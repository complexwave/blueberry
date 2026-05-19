#ifndef CI_MAP_C
#define CI_MAP_C
/*
 * ci_map.c — Citrin hashmap type (preindex + per-route max_psl variant)
 *
 * Robin Hood open-addressing.
 * Preindex: separate array (PREINDEX_MULT × buckets) of uint8_t that packs
 * two independent fields into one byte:
 *
 *   bits [2:0] = routing value (0 = unrouted/bloom miss, 1-7 = cache-line offset)
 *   bits [7:3] = max PSL of any key routed through this preindex entry
 *                (0 = no keys routed; 1-31 = max probe sequence length + 1)
 *
 * The routing field and max_psl are read from the SAME byte on lookup,
 * so the probe bound comes for free — no extra cache line.
 *
 * Backing buffer layout (contiguous):
 *   [ ci_map_kv × buckets ][ uint8_t × buckets × PREINDEX_MULT ]
 *
 * Empty slot: kvs[i].key == NULL.
 *
 * ci_ptr is void* for now; will become compressed pointer later.
 * No refcounting of contained keys/values (TODO).
 *
 * ---------------------------------------------------------------------------
 * Example:
 *
 *   ci_map *m = ci_map_new(16);
 *   ci_map_set(m, key, val);
 *   ci_ptr v = ci_map_get(m, key);
 *   ci_map_delete(m, key);
 * ---------------------------------------------------------------------------
 */

#include "ci_map.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ============================================================
 * Preindex configuration
 * ============================================================ */

#define CI_MAP_PREINDEX_MULT 1
#define CI_MAP_PREINDEX_MAX  7   /* routing values 1..7 fit in 3 bits; 0 = unrouted */

#define CI_MAP_PREINDEX_MAX_BITS 16
#define CI_MAP_PREINDEX_RSHIFT   0   /* 0=buckets, 1=buckets/2, 2=buckets/4 ... */

#define CI_MAP__PMASK_CAP  ((1u << CI_MAP_PREINDEX_MAX_BITS) - 1)
#define CI_MAP__PMASK(divmask) (((divmask) >> CI_MAP_PREINDEX_RSHIFT) & CI_MAP__PMASK_CAP)

/* ============================================================
 * Packed byte bit layout
 * ============================================================ */

#define CI_MAP__PREIDX_RMASK  0x07u   /* bits [2:0]: routing value */
#define CI_MAP__PREIDX_PMASK  0xF8u   /* bits [7:3]: max psl       */
#define CI_MAP__PREIDX_PSHIFT 3

/* Extract routing value (lower 3 bits). 0 = unrouted (bloom miss). */
static inline uint8_t ci_map__extract_preindex(uint8_t byte) {
	return byte & CI_MAP__PREIDX_RMASK;
}

/* Extract per-route max PSL (upper 5 bits). 0 = no keys routed. */
static inline uint8_t ci_map__extract_maxpsl(uint8_t byte) {
	return byte >> CI_MAP__PREIDX_PSHIFT;
}

/* space layout: [ ci_map_kv × buckets ][ uint8_t × buckets × PREINDEX_MULT ] */

#define CI_MAP_SCALE_FACTOR 2
#define CI_MAP_MIN_BUCKETS  8

/* ============================================================
 * Internal helpers
 * ============================================================ */

static inline ci_map_kv *ci_map__kvs(const ci_map *m) {
	return (ci_map_kv *)m->space;
}

static inline uint8_t *ci_map__preindex(const ci_map *m) {
	return (uint8_t *)((char *)m->space + sizeof(ci_map_kv) * (ci_map_buckets(m)));
}

static inline uint32_t ci_map__round_up2(uint32_t n) {
	n--;
	n |= n >> 1;
	n |= n >> 2;
	n |= n >> 4;
	n |= n >> 8;
	n |= n >> 16;
	return n + 1;
}


static inline uint32_t ci_map__hash_ptr(const void *ptr) {
	return (uintptr_t)ptr >> 5;
}

/* universal hash — dispatches by object type */
static inline uint32_t ci_hash(ci_ptr obj) {
	if (CI_IS_ANY_STR(obj))
		return ci_str_hash((ci_str *)obj);
	return ci_map__hash_ptr(obj);
}

/* identity comparison: pointer eq, then type-specific content eq */
static inline int ci_key_cmp(ci_ptr a, ci_ptr b) {
	if (a == b)
		return 1;
	if (CI_IS_ANY_STR(a) && CI_IS_ANY_STR(b))
		return ci_str_eq((const ci_str *)a, (const ci_str *)b);
	return 0;
}

/* ============================================================
 * hashcmp implementations
 * ============================================================ */

uint32_t ci_default_hashcmp(ci_ptr a, ci_ptr b) {
	if (!b)
		return ci_map__hash_ptr(a);
	if (a == b)
		return 1;
	return 0;
}

uint32_t ci_hashcmp_identity(ci_ptr a, ci_ptr b) {
	if (!b)
		return ci_hash(a);
	return (uint32_t)ci_key_cmp(a, b);
}

/* home position given pre-computed hash */
static inline uint32_t ci_map__home_pos(uint32_t h, const ci_map *m) {
	uint32_t  mask   = m->divmask;
	uint32_t  pmask  = CI_MAP__PMASK(mask);
	uint32_t  poffset = __builtin_popcount(pmask);
	uint8_t  *preidx = ci_map__preindex(m);

	uint32_t idx   = h & mask;
	uint32_t p_idx = (h >> (32 - poffset)) & pmask;
	uint8_t  p_val = ci_map__extract_preindex(preidx[p_idx]);
	return (idx + (uint32_t)p_val * 4) & mask;
}

static inline uint32_t ci_map__hash_key(const ci_map *m, ci_ptr key) {
	return m->hashcmp(key, NULL);
}

/* PSL of key sitting at pos */
static inline uint32_t ci_map__slot_psl(ci_ptr key, uint32_t pos, const ci_map *m) {
	return (pos - ci_map__home_pos(ci_map__hash_key(m, key), m)) & m->divmask;
}

/* Choose preindex routing by emptiness scoring.
 *
 * Checks 7 candidate windows (pval 1..7), each 8 slots wide, starting at
 * idx + pval*4.  All 7 windows span [idx+4, idx+35] = 32 bytes contiguous.
 *
 * Scalar: checks each candidate window for empty slots via kvs[].key == NULL.
 */
static uint8_t ci_map__choose_preindex(const ci_map *m, uint32_t idx) {
	ci_map_kv *kvs  = ci_map__kvs(m);
	uint32_t   mask = m->divmask;

	uint8_t  best_pval  = 1;
	uint32_t best_empty = 0;
	for (uint8_t pval = 1; pval <= CI_MAP_PREINDEX_MAX; pval++) {
		uint32_t start = (idx + (uint32_t)pval * 4) & mask;
		uint32_t empty = 0;
		
		for (uint32_t j = 0; j < 4; j++) {
			if (kvs[(start + j) & mask].key == NULL){
				empty++;
				if (empty > 2){
					return pval;
				}
			}
		}
		
		if (empty > best_empty) {
			best_empty = empty;
			best_pval  = pval;
		}
	}
	return best_pval;
}


/* ============================================================
 * Forward declarations
 * ============================================================ */

void     ci_map_register(void);
ci_map  *ci_map_new(uint32_t nbuckets);
int      ci_map_set(ci_map *m, ci_ptr key, ci_ptr val);
ci_ptr   ci_map_get(const ci_map *m, ci_ptr key);
int      ci_map_delete(ci_map *m, ci_ptr key);
void     ci_map_clear(ci_map *m);
ci_map_kv *ci_map_next(const ci_map *m, uint32_t *cursor);

/* identity-based API (hash + ci_key_cmp) */
ci_map_kv *ci_map_find_kv_hash(const ci_map *m, ci_ptr key, uint32_t h);
ci_ptr   ci_map_find(const ci_map *m, ci_ptr key);
int      ci_map_put(ci_map *m, ci_ptr key, ci_ptr val);
int      ci_map_remove(ci_map *m, ci_ptr key);

/* ============================================================
 * Registration
 * ============================================================ */

static void ci_map_destructor(void *ptr, tg_arena_t *arena) {
	(void)arena;
	ci_map *m = ptr;
	/* TODO: ci_dec all keys/values when refcounting elements */
	free(m->space);
	m->space = NULL;
}

void ci_map_register(void) {
	tg_arena_ops ops = { ci_map_destructor, NULL, NULL };
	ci_register_ops(CI_MAP, sizeof(ci_map), &ops);
}

/* ============================================================
 * Allocation / lifecycle
 * ============================================================ */

static size_t ci_map__space_size(uint32_t buckets) {
	return sizeof(ci_map_kv) * buckets
	     + sizeof(uint8_t)   * buckets * CI_MAP_PREINDEX_MULT + 128;
}

static inline ci_map *ci_map_init(ci_map *m, uint32_t nbuckets) {
	if (nbuckets < CI_MAP_MIN_BUCKETS) {
		nbuckets = CI_MAP_MIN_BUCKETS;
	}
	nbuckets = ci_map__round_up2(nbuckets);

	size_t sz = ci_map__space_size(nbuckets);
	void *mem = calloc(1, sz);
	if (!mem) {
		m->space = NULL;
		ci_free(m);
		return NULL;
	}

	m->divmask    = nbuckets - 1;
	m->used_limit = (uint32_t)(nbuckets * CI_MAP_LOAD_FACTOR);
	m->space      = mem;
	m->prototype  = NULL;
	m->hashcmp    = ci_default_hashcmp;
	return m;
}

ci_map *ci_map_new(uint32_t nbuckets) {
	ci_map *m = ci_new(CI_MAP);
	if (!m) return NULL;
	
	return ci_map_init(m, nbuckets);
}

ci_map *ci_map_ident_new(uint32_t nbuckets) {
	ci_map *m = ci_map_new(nbuckets);
	if (m) m->hashcmp = ci_hashcmp_identity;
	return m;
}

/* ============================================================
 * Resize
 * ============================================================ */

/* forward decl — insert without resize check (used during resize) */
static void ci_map__insert_noresize(ci_map *m, ci_ptr key, ci_ptr val, uint32_t h);

static int ci_map__resize(ci_map *m, uint32_t newbuckets) {
	if (newbuckets < CI_MAP_MIN_BUCKETS) {
		newbuckets = CI_MAP_MIN_BUCKETS;
	}
	newbuckets = ci_map__round_up2(newbuckets);

	ci_map_kv *old_kvs     = ci_map__kvs(m);
	uint32_t   old_buckets = ci_map_buckets(m);
	void      *old_space   = m->space;

	size_t sz = ci_map__space_size(newbuckets);
	void *mem = calloc(1, sz);
	if (!mem) return 0;

	m->space      = mem;
	m->divmask    = newbuckets - 1;
	m->used_limit = (uint32_t)(newbuckets * CI_MAP_LOAD_FACTOR);

	for (uint32_t i = 0; i < old_buckets; i++) {
		if (old_kvs[i].key != NULL) {
			ci_map__insert_noresize(m, old_kvs[i].key, old_kvs[i].val,
			                        ci_map__hash_key(m, old_kvs[i].key));
		}
	}

	free(old_space);
	return 1;
}

/* ============================================================
 * Robin Hood insert (no resize check)
 * ============================================================ */

static void ci_map__insert_noresize(ci_map *m, ci_ptr key, ci_ptr val, uint32_t h) {
	ci_map_kv *kvs    = ci_map__kvs(m);
	uint8_t   *preidx = ci_map__preindex(m);
	uint32_t   mask   = m->divmask;
	uint32_t   pmask  = CI_MAP__PMASK(mask);
	uint32_t   poffset = __builtin_popcount(pmask);

	uint32_t idx   = h & mask;
	uint32_t p_idx = (h >> (32 - poffset)) & pmask;

	/* get or choose preindex routing */
	uint8_t p_val = ci_map__extract_preindex(preidx[p_idx]);
	if (p_val == 0) {
		p_val = ci_map__choose_preindex(m, idx);
		preidx[p_idx] = (preidx[p_idx] & CI_MAP__PREIDX_PMASK) | p_val;
	}

	uint32_t pos = (idx + (uint32_t)p_val * 4) & mask;
	uint32_t psl = 0;
	
	ci_ptr k = kvs[pos].key;
	if (k == NULL) goto insert_to;
	if (k == key) goto replace_value;
	pos = (pos + 1) & mask; psl++;
	
	k = kvs[pos].key;
	if (k == NULL) goto insert_to;
	if (k == key) goto replace_value;
	pos = (pos + 1) & mask; psl++;
	
	k = kvs[pos].key;
	if (k == NULL) goto insert_to;
	if (k == key) goto replace_value;
	pos = (pos + 1) & mask; psl++;
	
	k = kvs[pos].key;
	if (k == NULL) goto insert_to;
	if (k == key) goto replace_value;
	pos = (pos + 1) & mask; psl++;
	
	while (1) {
		if (kvs[pos].key == NULL) goto insert_to;
		if (kvs[pos].key == key) goto replace_value;

		/* robin hood: displace if incumbent has shorter probe */
		uint32_t cur_psl = ci_map__slot_psl(kvs[pos].key, pos, m);
		if (cur_psl < psl) {
			ci_ptr tmp_key = kvs[pos].key;
			ci_ptr tmp_val = kvs[pos].val;

			kvs[pos].key = key;
			kvs[pos].val = val;

			/* update per-route max_psl for the key being placed */
			uint8_t cur_max = ci_map__extract_maxpsl(preidx[p_idx]);
			if (psl > cur_max) {
				uint8_t clamped = psl > 31 ? 31 : (uint8_t)psl;
				preidx[p_idx] = (preidx[p_idx] & CI_MAP__PREIDX_RMASK)
				              | (clamped << CI_MAP__PREIDX_PSHIFT);
			}
			key = tmp_key;
			val = tmp_val;
			psl = cur_psl;

			/* recompute p_idx for displaced key */
			uint32_t dh = ci_map__hash_key(m, key);
			p_idx = (dh >> (32 - poffset)) & pmask;
		}

		psl++;
		pos = (pos + 1) & mask;
	}
	
	insert_to:;
		uint8_t cur_max = ci_map__extract_maxpsl(preidx[p_idx]);
		if (psl > cur_max) {
			uint8_t clamped = psl > 31 ? 31 : (uint8_t)psl;
				preidx[p_idx] = (preidx[p_idx] & CI_MAP__PREIDX_RMASK)
						| (clamped << CI_MAP__PREIDX_PSHIFT);
		}
		
		m->used_limit--;
		kvs[pos].key = key;
	
	replace_value:
		kvs[pos].val = val;
		
	return;
}

/* ============================================================
 * Set (insert or replace)
 * ============================================================ */

int ci_map_set(ci_map *m, ci_ptr key, ci_ptr val) {
	if (m->used_limit == 0) {
		if (!ci_map__resize(m, (ci_map_buckets(m)) * CI_MAP_SCALE_FACTOR))
			return 0;
	}
	ci_map__insert_noresize(m, key, val, ci_map__hash_ptr(key));
	return 1;
}

/* ============================================================
 * Get (lookup)
 * ============================================================ */
// this one is fast but ptr only
__attribute__((always_inline))
static inline ci_map_kv *ci_map_find_kv_inline(const ci_map *m, ci_ptr key) {
	ci_map_kv *kvs    = ci_map__kvs(m);
	uint8_t   *preidx = ci_map__preindex(m);
	uint32_t   mask   = m->divmask;
	uint32_t   pmask  = CI_MAP__PMASK(mask);
	uint32_t   poffset = __builtin_popcount(pmask);

	uint32_t h     = ci_map__hash_ptr(key);
	uint32_t idx   = h & mask;
	uint32_t p_idx = (h >> (32 - poffset)) & pmask;

	uint8_t  p_byte = preidx[p_idx];
	uint8_t  p_val  = ci_map__extract_preindex(p_byte);

	if (p_val == 0) return NULL;  /* never routed → absent */

	int32_t  probes = (int32_t)ci_map__extract_maxpsl(p_byte);
	uint32_t pos    = (idx + (uint32_t)p_val * 4) & mask;

	ci_ptr k = kvs[pos].key;
	if (k == key) return &kvs[pos];
	pos = (pos + 1) & mask;

	k = kvs[pos].key;
	if (k == key) return &kvs[pos];
	pos = (pos + 1) & mask;

	k = kvs[pos].key;
	if (k == key) return &kvs[pos];;
	pos = (pos + 1) & mask;

	k = kvs[pos].key;
	if (k == key) return &kvs[pos];
	pos = (pos + 1) & mask;

	probes -= 4;
	if (probes < 0) return NULL;

	/* 31 = saturated, probe chain may be longer — fall back to NULL-termination */
	if (probes == 27) {  /* original maxpsl was 31 (31 - 4 = 27) */
		do {
			ci_ptr k = kvs[pos].key;
			if (k == key)  return &kvs[pos];
			if (k == NULL) return NULL;
			pos = (pos + 1) & mask;
		} while (1);
	}

	do {
		ci_ptr k = kvs[pos].key;
		if (k == key) return &kvs[pos];
		pos = (pos + 1) & mask;
		probes--;
	} while (probes >= 0);

	return NULL;
}

ci_map_kv *ci_map_find_kv(const ci_map *m, ci_ptr key) {
	return ci_map_find_kv_inline(m, key);
}

ci_ptr inline ci_map_get(const ci_map *m, ci_ptr key) {
	ci_map_kv *kv = ci_map_find_kv_inline(m, key);
	if(kv) return kv->val;
	return NULL;
}

/* ============================================================
 * Identity-based lookup (hash + ci_key_cmp)
 * ============================================================ */

ci_map_kv *ci_map_find_kv_hash(const ci_map *m, ci_ptr key, uint32_t h) {
	ci_map_kv *kvs    = ci_map__kvs(m);
	uint8_t   *preidx = ci_map__preindex(m);
	uint32_t   mask   = m->divmask;
	uint32_t   pmask  = CI_MAP__PMASK(mask);
	uint32_t   poffset = __builtin_popcount(pmask);

	uint32_t idx   = h & mask;
	uint32_t p_idx = (h >> (32 - poffset)) & pmask;

	uint8_t  p_byte = preidx[p_idx];
	uint8_t  p_val  = ci_map__extract_preindex(p_byte);

	if (p_val == 0) return NULL;

	int32_t  probes = (int32_t)ci_map__extract_maxpsl(p_byte);
	uint32_t pos    = (idx + (uint32_t)p_val * 4) & mask;

	if (probes == 31) probes = 0xFF;  /* saturated: clamp to large bound, rely on NULL */

	do {
		ci_ptr k = kvs[pos].key;
		if (k == NULL) return NULL;
		if (m->hashcmp(key, k)) return &kvs[pos];
		pos = (pos + 1) & mask;
		probes--;
	} while (probes >= 0);

	return NULL;
}

ci_ptr ci_map_find_hash(const ci_map *m, ci_ptr key) {
	return ci_map_find_kv_hash(m, key, m->hashcmp(key, NULL));
}

ci_ptr ci_map_find(const ci_map *m, ci_ptr key) {
	ci_map_kv *kv = ci_map_find_kv_hash(m, key, m->hashcmp(key, NULL));
	if (kv) return kv->val;
	return NULL;
}

int ci_map_put(ci_map *m, ci_ptr key, ci_ptr val) {
	uint32_t h = m->hashcmp(key, NULL);
	ci_map_kv *kv = ci_map_find_kv_hash(m, key, h);
	if (kv) {
		kv->val = val;
		return 1;
	}
	if (m->used_limit == 0) {
		if (!ci_map__resize(m, (ci_map_buckets(m)) * CI_MAP_SCALE_FACTOR))
			return 0;
	}
	ci_map__insert_noresize(m, key, val, h);
	return 1;
}





/* ============================================================
 * Delete (backward-shift)
 * ============================================================ */
static inline void ci_map_delete_kv(ci_map *m, ci_map_kv *kv);

int ci_map_delete(ci_map *m, ci_ptr key) {
	ci_map_kv *kv = ci_map_find_kv(m, key);
	if(!kv) return 0;
	
	ci_map_delete_kv(m, kv);
	return 1;
}

static inline void ci_map_delete_kv(ci_map *m, ci_map_kv *kv) {
	ci_map_kv *kvs    = ci_map__kvs(m);
	uint32_t   mask   = m->divmask;
	
	uint32_t pos = kv - kvs;
	
	/* backward-shift: pull subsequent entries back */
	while (1) {
		uint32_t next = (pos + 1) & mask;

		if (kvs[next].key == NULL ||
		    ci_map__slot_psl(kvs[next].key, next, m) == 0) {
			break;
		}

		kvs[pos].key = kvs[next].key;
		kvs[pos].val = kvs[next].val;
		
		pos = next;
	}

	kvs[pos].key = NULL;
	kvs[pos].val = NULL;

	m->used_limit++;
}

int ci_map_remove(ci_map *m, ci_ptr key) {
	ci_map_kv *kv = ci_map_find_kv_hash(m, key, m->hashcmp(key, NULL));
	if (!kv) return 0;
	ci_map_delete_kv(m, kv);
	return 1;
}

/* ============================================================
 * Utility
 * ============================================================ */

void ci_map_ensure_space(ci_map *m, uint32_t cnt) {
	if (m->used_limit >= cnt)
		return;

	uint32_t needed = ci_map_len(m) + cnt;
	ci_map__resize(m, (uint32_t)(needed / CI_MAP_LOAD_FACTOR) + 1);
}

void ci_map_clear(ci_map *m) {
	/* TODO: ci_dec all keys/values when refcounting */
	memset(m->space, 0, ci_map__space_size(ci_map_buckets(m)));
	m->used_limit = (uint32_t)(ci_map_buckets(m) * CI_MAP_LOAD_FACTOR);
}

/* iterate: call with *cursor = 0, returns NULL when done */
ci_map_kv *ci_map_next(const ci_map *m, uint32_t *cursor) {
	ci_map_kv *kvs = ci_map__kvs(m);

	for (uint32_t pos = *cursor; pos < ci_map_buckets(m); pos++) {
		if (kvs[pos].key != NULL) {
			*cursor = pos + 1;
			return &kvs[pos];
		}
	}
	return NULL;
}

/* ============================================================
 * String-keyed helpers (linear scan, no hashing)
 * ============================================================ */

ci_ptr ci_map_get_str(const ci_map *m, const char *key) {
	ci_map_kv *kvs = ci_map__kvs(m);

	for (uint32_t i = 0; i < ci_map_buckets(m); i++) {
		if (kvs[i].key != NULL &&
		    strcmp((const char *)kvs[i].key, key) == 0) {
			return kvs[i].val;
		}
	}
	return NULL;
}

int ci_map_set_str(ci_map *m, const char *key, ci_ptr val) {
	ci_map_kv *kvs = ci_map__kvs(m);

	/* update existing */
	for (uint32_t i = 0; i < ci_map_buckets(m); i++) {
		if (kvs[i].key != NULL &&
		    strcmp((const char *)kvs[i].key, key) == 0) {
			kvs[i].val = val;
			return 1;
		}
	}

	/* insert new — use the pointer-keyed path (key is the char*) */
	return ci_map_set(m, (ci_ptr)key, val);
}

void ci_map_dump(const ci_map *m) {
	uint32_t buckets = ci_map_buckets(m);
	uint32_t used    = ci_map_len(m);
	printf("ci_map(%p) buckets=%u used=%u (%.1f%%)\n", 
	       m, buckets, used, buckets ? (100.0 * used / buckets) : 0.0);
	uint32_t cursor = 0;
	ci_map_kv *kv;
	while ((kv = ci_map_next(m, &cursor)) != NULL)
		printf("  %p = %p\n", kv->key, kv->val);
}
#endif /* CI_MAP_C */
