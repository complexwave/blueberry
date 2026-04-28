/*
* ci_string.c — Citrin string types
*
* Two string representations:
*
*   ci_str_small  — inline string stored entirely in the tgmemlib arena slot.
*                   Has CI_GC_HDR. Four pool sizes: 32 / 64 / 128 / 256 bytes.
*                   Size = slot_size - sizeof(ci_str_small).
*
*   ci_str        — full dynamic string. Header in arena slot, data in malloc'd buffer.
*                   Refcountable. SKB-style four-pointer layout:
*
*     memory           start          end           limit
*       |<-head_space->|<----data---->|<-tail_space->|
*
* Accessors (ci_str_len, ci_str_size, ci_str_head, etc.) are polymorphic:
* they accept either ci_str * or ci_str_small * (via void *) and dispatch on
* the pointer tag.
*
* ---------------------------------------------------------------------------
* Example: zero-copy read(2) into a ci_str
*
*   ci_str  *buf  = ci_str_new(4096);
*   uint8_t *tail = ci_str_ensure_tail(buf, 4096); // ptr to tail, space guaranteed
*   ssize_t  n    = read(fd, tail, 4096);
*   if (n < 0) { perror("read"); ci_str_free(buf); return; }
*   ci_str_put_tail(buf, n);   // commit the bytes that actually arrived
*
* Example: prepend a protocol header (known size, so ensure_head does it all)
*
*   struct my_hdr *hdr = (struct my_hdr *)ci_str_ensure_head(buf, sizeof(*hdr));
*   hdr->type   = 0x01;                    // write directly into reserved head space
*   hdr->length = (uint16_t)ci_str_len(buf);
*   // start already retreated by sizeof(*hdr); data follows immediately
* ---------------------------------------------------------------------------
*/

#include "ciobj.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ============================================================
* Tag definitions  (16-bit tags: upper byte | lower byte)
* ============================================================
*
* CI_STR_FAMILY = CI_O_FAMILY_4 = 0x08 (bit 3 in ptrtag)
* Entry 0 via CI_FAMILY_ENTRY — same entry for full and small,
* upper byte distinguishes pool size for small variants.
*
* lower byte (ptrtag, bits 5..0):
*   full string:  CI_STR_FAMILY | CI_OBJECT | CI_REFCOUNTABLE = 0x0B
*   small string: CI_STR_FAMILY | CI_OBJECT                   = 0x09
*
* upper byte (not in pointer; type identity / pool routing only):
*   bits 14-15 = pool size index for small strings
*                  00 = 32 bytes  (CI_STR_SMALL_32)
*                  01 = 64 bytes  (CI_STR_SMALL_64)
*                  10 = 128 bytes (CI_STR_SMALL_128)
*                  11 = 256 bytes (CI_STR_SMALL_256)
*
* "Small" lives in ci_gchdr.flags as CI_OBJ_SMALL (bit 0).
* "Readonly/internalized" lives in ci_gchdr.flags as CI_OBJ_READONLY (bit 1).
* "Slice" lives in ci_gchdr.flags as CI_OBJ_SLICE (bit 2).
*   Slice is a ci_str_slice cast to ci_str *; shares CI_STR tag.
*   memory==start, limit==end — head/tail space both 0.
*   Mutation is blocked by CI_OBJ_READONLY (always set on slices).
*   parent field keeps the foreign memory owner alive via refcount.
*
*   tag     ptrtag  type
*   0x000B  0x0B    CI_STR             full string, refcountable
*   0x0009  0x09    CI_STR_SMALL_32    inline 32-byte slot
*   0x4009  0x09    CI_STR_SMALL_64    inline 64-byte slot
*   0x8009  0x09    CI_STR_SMALL_128   inline 128-byte slot
*   0xC009  0x09    CI_STR_SMALL_256   inline 256-byte slot
*
* All four small variants share ptrtag=0x09; upper byte drives pool selection.
*/

#define CI_STR_FAMILY    CI_O_FAMILY_4                      /* 0x08 */
#define CI_STR_TAG       CI_FAMILY_ENTRY(CI_STR_FAMILY, 0)  /* 0x08 */

#define CI_STR           ((uint16_t)(CI_STR_TAG | CI_OBJECT | CI_REFCOUNTABLE))         /* 0x000B */
#define CI_STR_SLICE     ((uint16_t)(CI_UPPER_TAG(0x01) | CI_STR_TAG | CI_OBJECT | CI_REFCOUNTABLE)) /* 0x010B */
#define CI_STR_SMALL_32  ((uint16_t)(CI_STR_TAG | CI_OBJECT))                          /* 0x0009 */
#define CI_STR_SMALL_64  ((uint16_t)(CI_UPPER_TAG(0x40) | CI_STR_TAG | CI_OBJECT))     /* 0x4009 */
#define CI_STR_SMALL_128 ((uint16_t)(CI_UPPER_TAG(0x80) | CI_STR_TAG | CI_OBJECT))     /* 0x8009 */
#define CI_STR_SMALL_256 ((uint16_t)(CI_UPPER_TAG(0xC0) | CI_STR_TAG | CI_OBJECT))     /* 0xC009 */

/* any string (full or small): family marker bit set */
#define CI_IS_ANY_STR(ptr)   CI_IS_FAMILY(ptr, CI_STR_FAMILY)
/* full heap string: any string that is not small */
#define CI_IS_STR(ptr)       (CI_IS_ANY_STR(ptr) && !CI_IS_SMALL(ptr))
/* small string: CI_OBJ_SMALL flag in gc header */
#define CI_IS_STR_SMALL(ptr) (((const ci_gchdr *)(ptr))->flags & CI_OBJ_SMALL)


/* ============================================================
* ci_str_small — inline small string
* ============================================================ */

typedef struct {
	CI_GC_HDR;         /* uint16_t refcnt, uint16_t flags — 4 bytes */
	uint8_t  length;   /* byte count of valid string data */
	uint8_t  data[];   /* inline bytes; total slot = tg_ptr_size() */
} ci_str_small;

/* ============================================================
* ci_str — full dynamic string (SKB-style)
* ============================================================ */

typedef struct {
	CI_GC_HDR;           /* uint16_t refcnt, uint16_t flags */
	uint32_t  hash;      /* cached hash of [start,end); 0 = not computed */
	uint8_t  *memory;    /* base of malloc'd buffer  (SKB: head) */
	uint8_t  *start;     /* first valid data byte    (SKB: data) */
	uint8_t  *end;       /* one past last data byte  (SKB: tail) */
	uint8_t  *limit;     /* one past end of buffer   (SKB: end)  */
} ci_str;

/* CI_OBJ_SLICE — set in gc.flags alongside CI_OBJ_READONLY for slice objects */
#define CI_OBJ_SLICE    (1 << 2)

#define CI_IS_SLICE(p)  (((const ci_gchdr *)(p))->flags & CI_OBJ_SLICE)

/*
 * ci_str_slice — foreign-memory view into a ci_str or any refcounted owner.
 *
 * Same tag as CI_STR (ptrtag 0x0B), allocated with sizeof(ci_str_slice).
 * gc.flags has CI_OBJ_SLICE | CI_OBJ_READONLY always set.
 * memory == start, limit == end — so head/tail space == 0 and all
 * existing size/space accessors work without any slice-specific branching.
 * parent keeps the backing memory alive; ci_dec'd in destructor.
 * parent may be NULL for slices into static/global memory.
 */


typedef struct {
	ci_str  slice;   /* must be first — cast to ci_str * for all accessors */
	
	ci_ptr  parent; // parent string is any
	
	ci_ptr  ctx; // additional context
} ci_str_slice;


/*
* ci_str_small_new(data, len)
*   Allocate the smallest pool that fits len and copy data in.
*   Selects 64 / 128 / 256 slot automatically (32-byte reserved for internalized).
*   Returns NULL if len exceeds largest pool capacity or alloc fails.
*   Returns ci_str * — caller never sees ci_str_small.
*/
ci_str *ci_str_small_new(const char *data, uint8_t len);


/* ============================================================
* Inline accessors — polymorphic over ci_str * and ci_str_small *
* ============================================================
*
* All accept void * and dispatch on the pointer tag.
* ci_str_head_space() returns 0 for small strings (no head space concept).
* ci_str_reset_hash() is a no-op for small strings (no hash field).
*/

/* number of used bytes */
static inline size_t ci_str_len(const void *p) {
	if (CI_IS_STR_SMALL(p))
		return ((const ci_str_small *)p)->length;
	
	return (size_t)(((const ci_str *)p)->end - ((const ci_str *)p)->start);
}

/* total buffer size (slot capacity for small, malloc'd buffer for full) */
static inline size_t ci_str_size(const void *p) {
	if (CI_IS_STR_SMALL(p))
		return (size_t)tg_ptr_size((void *)p) - sizeof(ci_str_small);
	
	return (size_t)(((const ci_str *)p)->limit - ((const ci_str *)p)->memory);
}

/* bytes available before data (always 0 for small strings) */
static inline size_t ci_str_head_space(const void *p) {
	if (CI_IS_STR_SMALL(p))
		return 0;
	
	return (size_t)(((const ci_str *)p)->start - ((const ci_str *)p)->memory);
}

/* bytes available after data */
static inline size_t ci_str_tail_space(const void *p) {
	if (CI_IS_STR_SMALL(p)) {
		const ci_str_small *s = p;
		return (size_t)tg_ptr_size((void *)p) - sizeof(ci_str_small) - s->length;
	}
	
	return (size_t)(((const ci_str *)p)->limit - ((const ci_str *)p)->end);
}

/* pointer to first valid byte */
static inline uint8_t *ci_str_head(void *p) {
	if (CI_IS_STR_SMALL(p))
		return ((ci_str_small *)p)->data;
	
	return ((ci_str *)p)->start;
}

/* pointer to one past last valid byte */
static inline uint8_t *ci_str_tail(void *p) {
	if (CI_IS_STR_SMALL(p)) {
		ci_str_small *s = p;
		return s->data + s->length;
	}
	return ((ci_str *)p)->end;
}

/* invalidate cached hash (no-op for small strings) */
static inline void ci_str_reset_hash(void *p) {
	if (!CI_IS_STR_SMALL(p))
		((ci_str *)p)->hash = 0;
}


/* ============================================================
* Registration
* ============================================================ */

/*
* ci_str_register()
*   Register all five string tags with the global ci_alloc.
*   Call once after ci_init(), before any string allocation.
*   CI_STR gets a destructor that frees its malloc'd backing buffer;
*   small strings are inline and need no destructor.
*/
void ci_str_register(void);


/* ============================================================
* Allocation / lifecycle
* ============================================================ */

/*
* ci_str_new(size)
*   Allocate a ci_str with malloc'd buffer of `size` bytes.
*   refcnt=1, hash=0 on return. Returns NULL on failure.
*/
ci_str *ci_str_new(size_t size);

/*
* ci_str_from_cstr(cstr)
*   Allocate ci_str and copy a C string into it.
*/
ci_str *ci_str_from_cstr(const char *cstr);

/*
* ci_str_copy(src, extra)
*   Deep copy of src's [start, end) into a new ci_str.
*   `extra` bytes of tail capacity reserved beyond copied content.
*   New string starts at memory base (no head space).
*/
ci_str *ci_str_copy(const void *src, size_t extra);

/*
 * ci_str_slice_new(data, len, parent)
 *   Create a readonly slice pointing into foreign memory [data, data+len).
 *   parent is ci_inc'd and ci_dec'd when the slice is freed.
 *   parent may be NULL for static/global memory with infinite lifetime.
 *   Returns ci_str * (tagged CI_STR, flagged CI_OBJ_SLICE|CI_OBJ_READONLY).
 */
ci_str *ci_str_slice_new(const uint8_t *data, size_t len, ci_ptr parent);

/*
* Use ci_free(s) for unconditional release (destructor frees memory buffer).
* Use ci_dec(s)  for refcount-aware release.
*/


/* ============================================================
* Buffer management
* ============================================================ */

/*
* ci_str_ensure_tail(s, n)
*   Guarantee at least n bytes of tail space. Reallocs if needed.
*   Growth policy: double, or exact-fit if request is large.
*   Returns pointer to current end (where caller writes), NULL on OOM.
*/
uint8_t *ci_str_ensure_tail(ci_str *s, size_t n);

/*
* ci_str_put_tail(s, n)
*   Commit n bytes at the tail: advance end by n. Resets hash.
*   Caller must have called ci_str_ensure_tail first.
*   Asserts tail_space >= n.
*/
void ci_str_put_tail(ci_str *s, size_t n);

/*
* ci_str_ensure_head(s, n)
*   Guarantee at least n bytes of head space. If insufficient:
*   malloc fresh buffer with extra headroom, copy data, free old.
*   Retreats start by n and returns new start. NULL on OOM.
*   Upgrades small strings in-place (fails on 32-byte / internalized).
*/
uint8_t *ci_str_ensure_head(ci_str *s, size_t n);

/*
* ci_str_compact(s)
*   Eliminate head space: memmove data to memory base, start = memory.
*   No realloc. Resets hash. No-op for small strings.
*/
void ci_str_compact(ci_str *s);


/* ============================================================
* Data operations
* ============================================================ */

/*
* ci_str_append(s, data, len)
*   Copy len bytes to tail. Calls ci_str_ensure_tail if needed.
*   Advances end. Resets hash. Returns 0 on OOM.
*/
int ci_str_append(ci_str *s, const void *data, size_t len);

/*
* ci_str_prepend(s, data, len)
*   Copy len bytes before start. Calls ci_str_ensure_head if needed.
*   Retreats start. Resets hash. Returns 0 on OOM.
*/
int ci_str_prepend(ci_str *s, const void *data, size_t len);

/*
* ci_str_rmhead(s, n)
*   Consume n bytes from head: advance start by n (no dealloc).
*   Clamped to current length. Resets hash. Returns bytes removed.
*/
size_t ci_str_rmhead(ci_str *s, size_t n);

/*
* ci_str_rmtail(s, n)
*   Consume n bytes from tail: retreat end by n (no dealloc).
*   Clamped to current length. Resets hash. Returns bytes removed.
*/
size_t ci_str_rmtail(ci_str *s, size_t n);

/*
* ci_str_clear(s)
*   Reset to empty: start = end = memory, hash = 0. No realloc.
*/
void ci_str_clear(ci_str *s);



/* ============================================================
* Hash / compare
* ============================================================ */

/*
* ci_str_hash(s)
*   Return cached hash; compute FNV-1a over [start, end) if hash == 0.
*   Stores 1 if computed hash happens to be 0 (avoids sentinel collision).
*/
uint32_t ci_str_hash(ci_str *s);

/*
* ci_str_eq(a, b)
*   Byte-equal comparison of two ci_str windows. Returns 1 if equal.
*/
int ci_str_eq(const ci_str *a, const ci_str *b);

/*
* ci_str_eq_cstr(s, cstr)
*   Compare [start, end) against a null-terminated C string.
*/
int ci_str_eq_cstr(const ci_str *s, const char *cstr);


/* ============================================================
* Implementations
* ============================================================ */

/* ---- Error / readonly guard ---- */

__attribute__((noreturn))
static void ci_string_error(const void *s, const char *msg) {
	const uint8_t *head = ci_str_head((void *)s);
	size_t len  = ci_str_len(s);
	size_t show = len < 20 ? len : 20;
	fprintf(stderr, "ci_str error: \"%.*s%s\" %s\n",
	        (int)show, (const char *)head, len > 20 ? "..." : "", msg);
	exit(1);
}

#define CI_STR_CHECK_WRITABLE(s) do { \
	if (__builtin_expect(CI_IS_READONLY(s), 0)) \
		ci_string_error(s, "is readonly"); \
} while(0)

/* ---- Registration ---- */

static void ci_str_destructor(void *ptr, tg_arena_t *arena) {
	(void)arena;
	ci_str *s = ptr;
	free(s->memory);
	s->memory = NULL;
}

static void ci_str_slice_destructor(void *ptr, tg_arena_t *arena) {
	(void)arena;
	ci_str_slice *sl = (ci_str_slice *)ptr;
	ci_dec(sl->parent);
	ci_dec(sl->ctx);
}

/* destructor for small string slots — only frees if upgraded to full */
static void ci_str_small_destructor(void *ptr, tg_arena_t *arena) {
	(void)arena;
	if (!(((ci_gchdr *)ptr)->flags & CI_OBJ_SMALL)) {
		ci_str *s = ptr;
		free(s->memory);
		s->memory = NULL;
	}
}

void ci_str_register(void) {
	tg_arena_ops str_ops       = { ci_str_destructor,       NULL, NULL };
	tg_arena_ops str_slice_ops = { ci_str_slice_destructor, NULL, NULL };
	tg_arena_ops str_small_ops = { ci_str_small_destructor, NULL, NULL };

	ci_register_ops(CI_STR,           sizeof(ci_str),       &str_ops);
	ci_register_ops(CI_STR_SLICE,     sizeof(ci_str_slice), &str_slice_ops);
	ci_register_ops(CI_STR_SMALL_32,  32,  &str_small_ops);
	ci_register_ops(CI_STR_SMALL_64,  64,  &str_small_ops);
	ci_register_ops(CI_STR_SMALL_128, 128, &str_small_ops);
	ci_register_ops(CI_STR_SMALL_256, 256, &str_small_ops);
}

/* ---- ci_str_small ---- */

ci_str *ci_str_small_new(const char *data, uint8_t len) {
	uint16_t cap = (uint16_t)sizeof(ci_str_small);
	uint16_t tag;

	if      (len <= 64  - cap) tag = CI_STR_SMALL_64;
	else if (len <= 128 - cap) tag = CI_STR_SMALL_128;
	else if (len <= 256 - cap) tag = CI_STR_SMALL_256;
	else return NULL;

	ci_str_small *s = ci_new(tag);
	if (!s) return NULL;

	/* ci_new doesn't init gc header for non-refcountable objects */
	s->gc.refcnt = 0;
	s->gc.flags  = CI_OBJ_SMALL;
	s->length = len;
	if (len) {
		memcpy(s->data, data, len);
	}
	return (ci_str *)s;
}

/*
 * ci_str_upgrade(p) — in-place upgrade from ci_str_small to ci_str.
 *
 * Rewrites the arena slot header from ci_str_small layout to ci_str layout.
 * Requires slot >= 64 bytes (sizeof(ci_str) = 40 on 64-bit).
 * Fails on 32-byte slots and readonly/internalized strings.
 * Returns the pointer reinterpreted as ci_str *, or NULL on failure.
 */
static ci_str *ci_str_upgrade(void *p) {
	if (CI_IS_READONLY(p)) {
		fprintf(stderr, "ci_str_upgrade: cannot upgrade internalized string\n");
		return NULL;
	}

	if (tg_ptr_size(p) < sizeof(ci_str)) {
		fprintf(stderr, "ci_str_upgrade: slot too small (%u bytes, need %zu)\n",
		        tg_ptr_size(p), sizeof(ci_str));
		return NULL;
	}

	ci_str_small *sm = p;
	uint8_t len = sm->length;

	/* allocate backing buffer — copy small string data into it */
	size_t alloc = len ? len : 1;
	uint8_t *mem = malloc(alloc);
	if (!mem) return NULL;

	if (len) {
		memcpy(mem, sm->data, len);
	}

	/* rewrite slot as ci_str */
	ci_str *s = p;
	s->gc.flags = 0;  /* clear CI_OBJ_SMALL */
	s->gc.refcnt = 0; /* not refcountable (tag bits unchanged) */
	s->hash   = 0;
	s->memory = mem;
	s->start  = mem;
	s->end    = mem + len;
	s->limit  = mem + alloc;
	return s;
}

/* ---- ci_str lifecycle ---- */

ci_str *ci_str_new(size_t size) {
	ci_str *s = ci_new(CI_STR);
	if (!s) return NULL;

	size_t alloc = size ? size : 1; /* avoid malloc(0) ambiguity */
	uint8_t *mem = malloc(alloc);
	if (!mem) {
		/* s->memory is uninitialised; zero it so destructor's free(NULL) is safe */
		s->memory = NULL;
		ci_free(s);
		return NULL;
	}

	s->hash   = 0;
	s->memory = mem;
	s->start  = mem;
	s->end    = mem;
	s->limit  = mem + alloc;
	return s;
}

ci_str *ci_str_from_cstr(const char *cstr) {
	size_t len = strlen(cstr);

	ci_str *s = ci_str_new(len);
	if (!s) return NULL;

	memcpy(s->start, cstr, len);
	s->end = s->start + len;
	return s;
}

ci_str *ci_str_copy(const void *src, size_t extra) {
	size_t len = ci_str_len(src);

	ci_str *s = ci_str_new(len + extra);
	if (!s) return NULL;

	memcpy(s->start, ci_str_head((void *)src), len);
	s->end = s->start + len;
	return s;
}

ci_str *ci_str_slice_new(const uint8_t *data, size_t len, ci_ptr parent) {
	ci_str_slice *sl = (ci_str_slice *)ci_new(CI_STR_SLICE);
	if (!sl) return NULL;

	/* ci_new sets refcnt=1, flags=0; OR in our flags */
	sl->slice.gc.flags = CI_OBJ_SLICE | CI_OBJ_READONLY;
	sl->slice.hash     = 0;
	sl->slice.memory   = (uint8_t *)data;  /* == start: head space = 0 */
	sl->slice.start    = (uint8_t *)data;
	sl->slice.end      = (uint8_t *)data + len;
	sl->slice.limit    = (uint8_t *)data + len;  /* == end: tail space = 0 */
	sl->parent         = parent;
	sl->ctx            = NULL;
	
	ci_inc(parent);

	return (ci_str *)sl;
}

/* ---- Buffer management ---- */

uint8_t *ci_str_ensure_tail(ci_str *s, size_t n) {
	CI_STR_CHECK_WRITABLE(s);

	if (CI_IS_STR_SMALL(s)) {
		ci_str_small *sm = (ci_str_small *)s;
		if (ci_str_tail_space(sm) >= n)
			return sm->data + sm->length;
		return NULL; /* can't grow: caller must upgrade first */
	}

	if (ci_str_tail_space(s) >= n)
		return s->end;

	size_t size    = ci_str_size(s);
	size_t end_off = (size_t)(s->end   - s->memory);
	size_t hd_off  = (size_t)(s->start - s->memory);
	size_t newsize = size * 2;
	if (newsize < end_off + n) {
		newsize = end_off + n;
	}

	uint8_t *newmem = realloc(s->memory, newsize);
	if (!newmem) return NULL;

	s->memory = newmem;
	s->start  = newmem + hd_off;
	s->end    = newmem + end_off;
	s->limit  = newmem + newsize;
	return s->end;
}

void ci_str_put_tail(ci_str *s, size_t n) {
	assert(ci_str_tail_space(s) >= n);
	CI_STR_CHECK_WRITABLE(s);

	if (CI_IS_STR_SMALL(s)) {
		ci_str_small *sm = (ci_str_small *)s;
		sm->length += (uint8_t)n;
	} else {
		s->end += n;
		ci_str_reset_hash(s);
	}
}

uint8_t *ci_str_ensure_head(ci_str *s, size_t n) {
	CI_STR_CHECK_WRITABLE(s);

	if (CI_IS_STR_SMALL(s)) {
		s = ci_str_upgrade(s);
		if (!s) return NULL;
	}

	if (ci_str_head_space(s) >= n) {
		ci_str_reset_hash(s);
		return s->start - n;
	}

	size_t len      = ci_str_len(s);
	size_t newsize  = n + len + ci_str_tail_space(s);
	uint8_t *newmem = malloc(newsize);
	if (!newmem) return NULL;

	memcpy(newmem + n, s->start, len);
	free(s->memory);

	s->memory = newmem;
	s->start  = newmem + n;
	s->end    = newmem + n + len;
	s->limit  = newmem + newsize;

	ci_str_reset_hash(s);
	return newmem;
}

void ci_str_compact(ci_str *s) {
	if (CI_IS_STR_SMALL(s)) return; /* already compact */

	size_t len = ci_str_len(s);

	if (s->start != s->memory) {
		memmove(s->memory, s->start, len);
		s->start = s->memory;
		s->end   = s->memory + len;
		ci_str_reset_hash(s);
	}
}

/* ---- Data operations ---- */

int ci_str_append(ci_str *s, const void *data, size_t len) {
	if (!ci_str_ensure_tail(s, len)) return 0;

	if (CI_IS_STR_SMALL(s)) {
		ci_str_small *sm = (ci_str_small *)s;
		memcpy(sm->data + sm->length, data, len);
		sm->length += (uint8_t)len;
	} else {
		memcpy(s->end, data, len);
		s->end += len;
		ci_str_reset_hash(s);
	}
	return 1;
}


int ci_str_prepend(ci_str *s, const void *data, size_t len) {
	uint8_t *head = ci_str_ensure_head(s, len); /* upgrades small in-place; checks readonly */
	if (!head) return 0;
	memcpy(head, data, len);
	s->start -= len;
	ci_str_reset_hash(s);
	return 1;
}
size_t ci_str_rmhead(ci_str *s, size_t n) {
	size_t len = ci_str_len(s);
	if (n > len) {
		n = len;
	}

	if (CI_IS_STR_SMALL(s)) {
		ci_str_small *sm = (ci_str_small *)s;
		sm->length -= (uint8_t)n;
		if (sm->length) {
			memmove(sm->data, sm->data + n, sm->length);
		}
	} else {
		s->start += n;
		if (s->start == s->end) {
			s->start = s->end = s->memory;
		}
		ci_str_reset_hash(s);
	}
	return n;
}
size_t ci_str_rmtail(ci_str *s, size_t n) {
	size_t len = ci_str_len(s);
	if (n > len) {
		n = len;
	}

	if (CI_IS_STR_SMALL(s)) {
		ci_str_small *sm = (ci_str_small *)s;
		sm->length -= (uint8_t)n;
	} else {
		s->end -= n;
		if (s->start == s->end) {
			s->start = s->end = s->memory;
		}
		ci_str_reset_hash(s);
	}
	return n;
}
void ci_str_clear(ci_str *s) {
	if (CI_IS_STR_SMALL(s)) {
		ci_str_small *sm = (ci_str_small *)s;
		sm->length = 0;
		sm->data[0] = 0;
	} else {
		s->start = s->memory;
		s->end   = s->memory;
		s->hash  = 0;
		s->memory[0] = 0;
	}
}

void ci_str_clear_headroom(ci_str *s, size_t headroom) {
	ci_str_clear(s);
	if (headroom) {
		ci_str_ensure_head(s, headroom);
	}
}

/* ---- Hash / compare ---- */

static uint32_t ci_str_fnv1a(const uint8_t *data, size_t len) {
	uint32_t h = 2166136261u;
	for (size_t i = 0; i < len; i++) {
		h ^= (uint32_t)data[i];
		h *= 16777619u;
	}
	return h ? h : 1; /* 0 reserved as "not computed" sentinel */
}

uint32_t ci_str_hash(ci_str *s) {
	if (CI_IS_STR_SMALL(s)) {
		ci_str_small *sm = (ci_str_small *)s;
		return ci_str_fnv1a(sm->data, sm->length);
	}

	if (s->hash != 0) return s->hash;
	s->hash = ci_str_fnv1a(s->start, ci_str_len(s));
	return s->hash;
}
int ci_str_eq(const ci_str *a, const ci_str *b) {
	size_t la = ci_str_len(a);
	size_t lb = ci_str_len(b);
	if (la != lb) return 0;

	return memcmp(ci_str_head((void *)a), ci_str_head((void *)b), la) == 0;
}
int ci_str_eq_cstr(const ci_str *s, const char *cstr) {
	size_t len = ci_str_len(s);
	const uint8_t *head = ci_str_head((void *)s);

	if (strncmp((const char *)head, cstr, len) != 0) return 0;
	if (cstr[len] != '\0') return 0;

	return 1;
}
