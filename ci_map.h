#ifndef CI_MAP_H
#define CI_MAP_H
/*
 * ci_map.h — Citrin hashmap public API
 *
 * Robin Hood open-addressing with preindex routing.
 * Two lookup modes:
 *
 *   Pointer-identity (fast path)
 *     Keys compared by pointer equality (==).
 *     Use for interned symbols and non-string ciobjects.
 *     Functions: ci_map_set, ci_map_get, ci_map_find_kv, ci_map_delete
 *
 *   Object-identity
 *     Keys hashed by content (ci_hash) and compared via ci_key_cmp.
 *     ci_str keys match by string content, everything else by pointer.
 *     Functions: ci_map_put, ci_map_find, ci_map_find_kv_hash, ci_map_remove
 *
 * Do not mix both modes on the same map with string keys.
 *
 * ---------------------------------------------------------------------------
 * Example (pointer-identity):
 *
 *   ci_map *m = ci_map_new(16);
 *   ci_map_set(m, key, val);
 *   ci_ptr v = ci_map_get(m, key);
 *   ci_map_delete(m, key);
 *
 * Example (object-identity):
 *
 *   ci_map *m = ci_map_new(16);
 *   ci_map_put(m, str_key, val);
 *   ci_ptr v = ci_map_find(m, other_str_with_same_content);
 *   ci_map_remove(m, str_key);
 * ---------------------------------------------------------------------------
 */

#include "ciobj.h"
#include <stdint.h>

/* ============================================================
 * Types
 * ============================================================ */

typedef void *ci_ptr;

typedef struct {
	ci_ptr key;
	ci_ptr val;
} ci_map_kv;

typedef struct {
	CI_GC_HDR;
	uint32_t used_limit;  /* countdown: -- on insert, ++ on delete, resize at 0 */
	uint32_t divmask;     /* buckets - 1 */
	void    *space;
	ci_ptr  prototype;
	/*
	 * hashcmp — dual-mode function pointer:
	 *   (a, NULL) → hash of a (uint32_t)
	 *   (a, b)    → 1 if a == b, 0 otherwise
	 * Keys are never NULL so the b==NULL sentinel is safe.
	 */
	uint32_t (*hashcmp)(ci_ptr a, ci_ptr b);
} ci_map;

/* ============================================================
 * Tag definitions
 * ============================================================ */

#define CI_MAP_FAMILY   CI_O_FAMILY_4
#define CI_MAP_TAG      CI_FAMILY_ENTRY(CI_MAP_FAMILY, 0)

#define CI_MAP          ((uint16_t)(CI_MAP_TAG | CI_OBJECT | CI_REFCOUNTABLE))

#define CI_IS_ANY_MAP(ptr)  CI_CHECK_MASK_FAMILY(ptr, CI_MAP_FAMILY | CI_OBJECT, CI_MAP_FAMILY)
#define CI_IS_MAP(ptr)      CI_CHECK_MASK_FAMILY(ptr, CI_MAP_TAG | CI_OBJECT, CI_MAP_FAMILY)

#ifndef CI_MAP_LOAD_FACTOR
#define CI_MAP_LOAD_FACTOR  0.7
#endif

/* ============================================================
 * Inline accessors
 * ============================================================ */

static inline uint32_t ci_map_buckets(const ci_map *m) {
	return m->divmask + 1;
}

static inline uint32_t ci_map_len(const ci_map *m) {
	return (uint32_t)(ci_map_buckets(m) * CI_MAP_LOAD_FACTOR) - m->used_limit;
}

/* ============================================================
 * hashcmp implementations
 * ============================================================ */

/*
 * ci_default_hashcmp — pointer-identity mode (fast path).
 *   (a, NULL) → pointer hash
 *   (a, b)    → 1 if a == b
 * Used by ci_map_new.
 */
uint32_t ci_default_hashcmp(ci_ptr a, ci_ptr b);

/*
 * ci_hashcmp_identity — object-identity mode.
 *   (a, NULL) → ci_hash(a)  (content hash for ci_str, pointer hash otherwise)
 *   (a, b)    → ci_key_cmp(a, b)  (content equality for ci_str)
 * Set this on maps that use ci_map_put / ci_map_find / ci_map_remove.
 */
uint32_t ci_hashcmp_identity(ci_ptr a, ci_ptr b);

/* ============================================================
 * Lifecycle
 * ============================================================ */

/* Register CI_MAP type with the arena allocator. Call once at init. */
void ci_map_register(void);

/* Allocate a new map with pointer-identity hashcmp. nbuckets rounded up to power of 2 (min 8). */
ci_map *ci_map_new(uint32_t nbuckets);

/* Allocate a new map with object-identity hashcmp (ci_hashcmp_identity). */
ci_map *ci_map_ident_new(uint32_t nbuckets);

/* ============================================================
 * Pointer-identity API (fast path, == comparison)
 * ============================================================ */

/*
 * ci_map_set — insert or replace by pointer identity.
 *   Returns 1 on success, 0 on allocation failure.
 */
int ci_map_set(ci_map *m, ci_ptr key, ci_ptr val);

/*
 * ci_map_get — lookup by pointer identity.
 *   Returns value or NULL.
 */
ci_ptr ci_map_get(const ci_map *m, ci_ptr key);

/*
 * ci_map_find_kv — lookup by pointer identity, returns kv pair.
 *   Returned pointer invalidated by any mutation.
 */
ci_map_kv *ci_map_find_kv(const ci_map *m, ci_ptr key);

/*
 * ci_map_delete — remove by pointer identity.
 *   Returns 1 if removed, 0 if absent.
 */
int ci_map_delete(ci_map *m, ci_ptr key);

/* ============================================================
 * Object-identity API (ci_hash + ci_key_cmp)
 * ============================================================ */

/*
 * ci_map_put — insert or replace by object identity.
 *   ci_str keys with same content are the same key.
 *   Returns 1 on success, 0 on allocation failure.
 */
int ci_map_put(ci_map *m, ci_ptr key, ci_ptr val);

/*
 * ci_map_find — lookup by object identity.
 *   Returns value or NULL.
 */
ci_ptr ci_map_find(const ci_map *m, ci_ptr key);

/*
 * ci_map_find_kv_hash — lookup by object identity with pre-computed hash.
 *   Pass h = ci_hash(key). Returns kv pair or NULL.
 */
ci_map_kv *ci_map_find_kv_hash(const ci_map *m, ci_ptr key, uint32_t h);

/*
 * ci_map_remove — remove by object identity.
 *   Returns 1 if removed, 0 if absent.
 */
int ci_map_remove(ci_map *m, ci_ptr key);

/* ============================================================
 * Utility
 * ============================================================ */

/* Remove all entries. Does not shrink backing buffer. */
void ci_map_clear(ci_map *m);

/*
 * ci_map_next — iterate entries.
 *   Init *cursor = 0. Returns NULL when done.
 *   Do not mutate during iteration.
 */
ci_map_kv *ci_map_next(const ci_map *m, uint32_t *cursor);

/* ============================================================
 * String-keyed helpers (linear scan, strcmp, O(n))
 * Prefer ci_map_put with ci_str keys for hashed lookup.
 * ============================================================ */

ci_ptr ci_map_get_str(const ci_map *m, const char *key);
int    ci_map_set_str(ci_map *m, const char *key, ci_ptr val);

#endif /* CI_MAP_H */
