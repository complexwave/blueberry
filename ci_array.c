/*
 * ci_array.c — Citrin array type
 *
 * Two representations:
 *
 *   ci_arr_small  — inline array stored in tgmemlib arena slot.
 *                   Has CI_GC_HDR. Four pool sizes: 128 / 256 / 1024 / 2048 bytes.
 *                   Capacity = (slot_size - sizeof(ci_array)) / sizeof(ci_ptr).
 *
 *   ci_array      — full dynamic array. Header in arena slot, data in malloc'd buffer.
 *                   Refcountable.
 *
 * Both use circular buffer layout:
 *   element[i] = data[(offset + i) % size]
 *
 * This gives O(1) push/pop at both ends (push/pop = tail, shift/unshift = head).
 *
 * ci_ptr is void* for now; will become compressed pointer later.
 *
 * ---------------------------------------------------------------------------
 * Example:
 *
 *   ci_array *a = ci_arr_new(16);
 *   ci_arr_push(a, some_obj);       // append at tail
 *   ci_arr_unshift(a, other_obj);   // prepend at head
 *   void *v = ci_arr_index(a, 0);   // random access
 *   ci_arr_pop(a);                  // remove from tail
 *   ci_arr_shift(a);                // remove from head
 *   ci_arr_append(a, other_arr);    // append all elements of other_arr
 * ---------------------------------------------------------------------------
 */

#include "ciobj.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ============================================================
 * Types
 * ============================================================ */

typedef void *ci_ptr;

/* ============================================================
 * Tag definitions (16-bit tags: upper byte | lower byte)
 * ============================================================
 *
 * All arrays are refcountable. The ptrtag uses family field to fast-guard
 * that this is an array. The upper byte carries exact type identity:
 * full vs small, and pool size for small.
 *
 * CI_ARRAY_FAMILY = CI_O_FAMILY_1 = 0x20 (bit 5 in ptrtag)
 * CI_ARR_TAG = CI_FAMILY_ENTRY(CI_ARRAY_FAMILY, 0) = 0x20
 *
 * full array:  CI_ARR = 0x20 | CI_OBJECT | CI_REFCOUNTABLE = 0x23
 * small array: CI_ARR_SMALL_* = 0x20 | CI_OBJECT = 0x21 + upper byte pool size
 *
 * upper byte bits (not in pointer; type identity only):
 *   bits 14-15  = pool size index for inline arrays
 *                   00 = 128 bytes
 *                   01 = 256 bytes
 *                   10 = 1024 bytes
 *                   11 = 2048 bytes
 *
 * "Small" also lives in ci_gchdr.flags as CI_OBJ_SMALL (bit 0).
 *
 *   tag     hex       ptrtag  type
 *   0x0023  ...         0x23  CI_ARR              full heap array, refcountable
 *   0x0021  ...         0x21  CI_ARR_SMALL_128    inline 128-byte slot
 *   0x4021  ...         0x21  CI_ARR_SMALL_256    inline 256-byte slot
 *   0x8021  ...         0x21  CI_ARR_SMALL_1024   inline 1024-byte slot
 *   0xC021  ...         0x21  CI_ARR_SMALL_2048   inline 2048-byte slot
 *
 * All five variants share family bit 0x20; upper byte distinguishes pool size.
 */

#define CI_ARRAY_FAMILY      CI_O_FAMILY_1
#define CI_ARR_TAG           CI_FAMILY_ENTRY(CI_ARRAY_FAMILY, 0)
#define CI_ARR_DEFAULT_CAP   8

#define CI_ARR            ((uint16_t)(CI_ARR_TAG | CI_REFCOUNTABLE))              /* 0x0022 */
#define CI_ARR_SMALL_128  ((uint16_t)(CI_ARR_TAG))                                /* 0x0020 */
#define CI_ARR_SMALL_256  ((uint16_t)(CI_UPPER_TAG(0x40) | CI_ARR_TAG))           /* 0x4020 */
#define CI_ARR_SMALL_1024 ((uint16_t)(CI_UPPER_TAG(0x80) | CI_ARR_TAG))           /* 0x8020 */
#define CI_ARR_SMALL_2048 ((uint16_t)(CI_UPPER_TAG(0xC0) | CI_ARR_TAG))           /* 0xC020 */

/* any array (full or small): family marker bit set */
#define CI_IS_ANY_ARR(ptr)   CI_IS_FAMILY(ptr, CI_ARRAY_FAMILY)
/* full heap array: any array that is not small */
#define CI_IS_ARR(ptr)       (CI_IS_ANY_ARR(ptr) && !CI_IS_SMALL(ptr))
/* small inline array: CI_OBJ_SMALL flag in gc header */
#define CI_IS_ARR_SMALL(ptr) (((const ci_gchdr *)(ptr))->flags & CI_OBJ_SMALL)

/* ============================================================
 * ci_array — unified struct for both full and inline
 * ============================================================
 *
 * For inline (small) arrays:
 *   data points to inhdr_data[]
 *   size = (tg_ptr_size() - offsetof(ci_array, inhdr_data)) / sizeof(ci_ptr)
 *   CI_OBJ_SMALL set in gc.flags
 *
 * For full arrays:
 *   data points to malloc'd buffer
 *   size is the malloc'd capacity in elements
 */

typedef struct {
	CI_GC_HDR;            /* uint16_t refcnt, uint16_t flags — 4 bytes */
	uint32_t size;        /* capacity in elements */
	uint32_t length;      /* number of valid elements */
	uint32_t offset;      /* circular buffer: index of first element */
	ci_ptr  *data;        /* backing store (malloc'd or &inhdr_data) */
	ci_ptr   inhdr_data[];/* flexible array for inline storage */
} ci_array;


/* ============================================================
 * Inline accessors
 * ============================================================ */

static inline uint32_t ci_arr_len(const ci_array *a) {
	return a->length;
}

static inline uint32_t ci_arr_size(const ci_array *a) {
	return a->size;
}

static inline uint32_t ci_arr_space(const ci_array *a) {
	return a->size - a->length;
}

/* circular buffer index mapping */
static inline uint32_t ci_arr__idx(const ci_array *a, uint32_t i) {
	return (a->offset + i) % a->size;
}

static inline void ci_arr__zero_tail(ci_array *a, uint32_t from, uint32_t to) {
	if (a->offset == 0) {
		memset(a->data + from, 0, (to - from) * sizeof(ci_ptr));
	} else {
		for (uint32_t i = from; i < to; i++)
			a->data[ci_arr__idx(a, i)] = NULL;
	}
}

/*
 * ci_arr_rmoffset(a, dst) — linearize circular buffer into dst.
 *
 * dst must hold at least a->length elements. Uses memmove for each
 * contiguous segment (1 call if not wrapped, 2 if wrapped).
 */
#define CI_ARR_IS_WRAPPED(a) (((a)->length > 0) && (a)->offset + (a)->length > (a)->size)

#define CI_ARR_UNWRAPPED_HEAD(a) ((a)->data + (a)->offset)
#define CI_ARR_UNWRAPPED_LENGTH(a) ((a)->length)

#define CI_ARR_WRAPPED_SEG1_HEAD(a)       ((a)->data + (a)->offset)
#define CI_ARR_WRAPPED_SEG1_LENGTH(a)     ((a)->size - (a)->offset)

#define CI_ARR_WRAPPED_SEG2_HEAD(a)       ((a)->data)
#define CI_ARR_WRAPPED_SEG2_LENGTH(a)     ((a)->length - CI_ARR_WRAPPED_SEG1_LENGTH(a))

static inline void ci_arr_rmoffset(const ci_array *a, ci_ptr *dst) {
	if (a->length == 0) return;
	if (!CI_ARR_IS_WRAPPED(a)) {
		memmove(dst, CI_ARR_UNWRAPPED_HEAD(a), CI_ARR_UNWRAPPED_LENGTH(a) * sizeof(ci_ptr));
	} else {
		uint32_t seg1_length = CI_ARR_WRAPPED_SEG1_LENGTH(a);
		uint32_t seg2_length = a->length - seg1_length;
		
		memmove(dst,               CI_ARR_WRAPPED_SEG1_HEAD(a), seg1_length * sizeof(ci_ptr));
		memmove(dst + seg1_length, CI_ARR_WRAPPED_SEG2_HEAD(a), seg2_length * sizeof(ci_ptr));
	}
}

/* lvalue macro: ci_arr(a, i) = val  or  val = ci_arr(a, i) */
#define ci_arr(a, i) ((a)->data[ci_arr__idx((a), (i))])

/* random access: returns element at logical index i */
static inline ci_ptr ci_arr_index(const ci_array *a, uint32_t i) {
	assert(i < a->length);
	return ci_arr(a, i);
}

/* wrap negative index: -1 → last, -2 → second-to-last, etc.
 * returns a->length (invalid) if result is still out of bounds. */
static inline uint32_t ci_arr_wrapindex(const ci_array *a, intptr_t index) {
	if (index >= 0) return (uint32_t)index;
	intptr_t wrapped = (intptr_t)a->length + index;
	if (wrapped < 0) return a->length;
	return (uint32_t)wrapped;
}

static ci_array *ci_arr_upgrade(ci_array *a);  /* defined below */

/* Allocate a fresh buffer of newsize, linearize into it, replace a->data.
 * Zeros tail beyond length. Frees old a->data.
 * Must NOT be called on small (inline) arrays — a->data is not malloc'd there. */
static int ci_arr__linearize(ci_array *a, uint32_t newsize) {
	ci_ptr *mem = malloc(newsize * sizeof(ci_ptr));
	if (!mem) return 0;
	
	ci_arr_rmoffset(a, mem);
	
	if (newsize > a->length)
		memset(mem + a->length, 0, (newsize - a->length) * sizeof(ci_ptr));
	
	free(a->data);
	
	a->data   = mem;
	a->size   = newsize;
	a->offset = 0;
	
	return 1;
}

/* Ensure elements are in a contiguous region.
 * No-op when not wrapped. For small arrays, upgrades first. */
static int ci_arr_ensure_continuous(ci_array *a) {
	if (a->gc.flags & CI_OBJ_SMALL) {
		if (!ci_arr_upgrade(a)) return 0;
		return 1; /* upgrade always linearizes */
	}
	if (!CI_ARR_IS_WRAPPED(a)) return 1;
	
	return ci_arr__linearize(a, a->size);
}

/* Direct pointer to the first element.
 * Linearizes only if wrapped; non-wrapped shifted arrays return data+offset as-is. */
static inline ci_ptr *ci_arr_head(ci_array *a) {
	ci_arr_ensure_continuous(a);
	return a->data + a->offset;
}

/* write at logical index i */
static inline void ci_arr_set(ci_array *a, uint32_t i, ci_ptr val) {
	assert(i < a->length);
	/* TODO: ci_dec old element, ci_inc val — when refcounting elements */
	ci_arr(a, i) = val;
}


/* ============================================================
 * Forward declarations
 * ============================================================ */

void      ci_arr_register(void);
ci_array *ci_arr_new(uint32_t size);
ci_array *ci_arr_new_inline(uint32_t nelems);
int       ci_arr_ensure_space(ci_array *a, uint32_t n);
int       ci_arr_push(ci_array *a, ci_ptr val);
ci_ptr    ci_arr_pop(ci_array *a);
int       ci_arr_unshift(ci_array *a, ci_ptr val);
ci_ptr    ci_arr_shift(ci_array *a);
int       ci_arr_append(ci_array *dst, const ci_array *src);
void      ci_arr_clear(ci_array *a);
void      ci_arr_reverse(ci_array *a);
int32_t   ci_arr_find(ci_array *a, ci_ptr element, uint32_t start_idx);
int       ci_arr_contains(ci_array *a, ci_ptr element);
ci_array *ci_arr_slice(ci_array *a, int32_t from, int32_t to);
ci_array *ci_arr_copy(ci_array *a);
int       ci_arr_eq(const ci_array *a, const ci_array *b);


/* ============================================================
 * Registration
 * ============================================================ */

/*
 * ci_dec all live elements in the circular buffer.
 *
 *  no wrap:   [____XXXXXXXX____]
 *                  ^offset  ^offset+length
 *                  |--seg1--|
 *
 *  wrapped:   [XXXX________XXXX]
 *              ^seg2       ^offset
 *              |--|        |seg1|
 */
static void ci_arr_destructor(void *ptr, tg_arena_t *arena) {
	(void)arena;
	ci_array *a = ptr;

	if (a->offset + a->length <= a->size) {
		ci_ptr *p = a->data + a->offset;
		uint32_t count = a->length;
		while (count--) { ci_dec(*p); p++; }
	} else {
		uint32_t seg1 = a->size - a->offset;
		ci_ptr *p = a->data + a->offset;
		uint32_t count = seg1;
		while (count--) { ci_dec(*p); p++; }

		p = a->data;
		count = a->length - seg1;
		while (count--) { ci_dec(*p); p++; }
	}

	free(a->data);
	a->data = NULL;
}

/* destructor for small array slots — only frees if upgraded to full */
/* same circular-buffer dec logic, only frees data if upgraded from small */
static void ci_arr_small_destructor(void *ptr, tg_arena_t *arena) {
	(void)arena;
	ci_array *a = ptr;

	if (a->offset + a->length <= a->size) {
		ci_ptr *p = a->data + a->offset;
		uint32_t count = a->length;
		while (count--) { ci_dec(*p); p++; }
	} else {
		uint32_t seg1 = a->size - a->offset;
		ci_ptr *p = a->data + a->offset;
		uint32_t count = seg1;
		while (count--) { ci_dec(*p); p++; }

		p = a->data;
		count = a->length - seg1;
		while (count--) { ci_dec(*p); p++; }
	}

	if (!(a->gc.flags & CI_OBJ_SMALL)) {
		free(a->data);
		a->data = NULL;
	}
}

void ci_arr_register(void) {
	tg_arena_ops arr_ops       = { ci_arr_destructor, NULL, NULL };
	tg_arena_ops arr_small_ops = { ci_arr_small_destructor, NULL, NULL };

	ci_register_ops(CI_ARR,            sizeof(ci_array), &arr_ops);
	ci_register_ops(CI_ARR_SMALL_128,  128,  &arr_small_ops);
	ci_register_ops(CI_ARR_SMALL_256,  256,  &arr_small_ops);
	ci_register_ops(CI_ARR_SMALL_1024, 1024, &arr_small_ops);
	ci_register_ops(CI_ARR_SMALL_2048, 2048, &arr_small_ops);
}


/* ============================================================
 * Allocation / lifecycle
 * ============================================================ */

ci_array *ci_arr_new(uint32_t size) {
	ci_array *a = ci_new(CI_ARR);
	if (!a) return NULL;

	uint32_t alloc = size ? size : CI_ARR_DEFAULT_CAP;
	ci_ptr *mem = malloc(alloc * sizeof(ci_ptr));
	if (!mem) {
		a->data = NULL;
		ci_free(a);
		return NULL;
	}

	a->size   = alloc;
	a->length = 0;
	a->offset = 0;
	a->data   = mem;
	return a;
}

ci_array *ci_arr_new_inline(uint32_t nelems) {
	/* pick smallest pool that fits nelems pointers */
	uint32_t need = (uint32_t)(offsetof(ci_array, inhdr_data) + nelems * sizeof(ci_ptr));
	uint16_t tag;

	if      (need <= 128)  tag = CI_ARR_SMALL_128;
	else if (need <= 256)  tag = CI_ARR_SMALL_256;
	else if (need <= 1024) tag = CI_ARR_SMALL_1024;
	else if (need <= 2048) tag = CI_ARR_SMALL_2048;
	else return NULL;

	ci_array *a = ci_new(tag);
	if (!a) return NULL;

	uint32_t slot = tg_ptr_size(a);
	uint32_t cap  = (slot - (uint32_t)offsetof(ci_array, inhdr_data)) / (uint32_t)sizeof(ci_ptr);

	a->gc.flags  = CI_OBJ_SMALL;
	a->size      = cap;
	a->length    = 0;
	a->offset    = 0;
	a->data      = a->inhdr_data;
	return a;
}

/*
 * ci_arr_upgrade(a) — in-place upgrade from inline to full array.
 *
 * Linearizes the circular buffer into a fresh malloc'd buffer.
 * Clears CI_OBJ_SMALL. Returns the same pointer reinterpreted, or NULL on failure.
 */
static ci_array *ci_arr_upgrade(ci_array *a) {
	if (!(a->gc.flags & CI_OBJ_SMALL)) return a; /* already full */

	uint32_t cap = a->size;
	if (cap < a->length) cap = a->length;
	if (cap == 0)        cap = CI_ARR_DEFAULT_CAP;

	ci_ptr *mem = malloc(cap * sizeof(ci_ptr));
	if (!mem) return NULL;

	ci_arr_rmoffset(a, mem);

	a->gc.flags &= ~(uint16_t)CI_OBJ_SMALL;
	a->data   = mem;
	a->size   = cap;
	a->offset = 0;
	return a;
}


/* ============================================================
 * Buffer management
 * ============================================================ */

/*
 * ci_arr_ensure_space(a, n) — guarantee room for n more elements.
 *
 * For inline arrays: upgrades to full if current capacity insufficient.
 * For full arrays: realloc + linearize if needed.
 * Returns 1 on success, 0 on OOM.
 */
int ci_arr_ensure_space(ci_array *a, uint32_t n) {
	if (a->length + n <= a->size) return 1;

	/* inline: must upgrade first */
	if (a->gc.flags & CI_OBJ_SMALL) {
		uint32_t need = a->length + n;
		/* upgrade gives us at least a->size elements; if not enough we'll realloc below */
		ci_array *upgraded = ci_arr_upgrade(a);
		if (!upgraded) return 0;
		/* a == upgraded (in-place), check if upgrade gave enough */
		if (a->length + n <= a->size) return 1;
		/* fall through to realloc */
		(void)need;
	}

	/* full array: realloc + linearize */
	uint32_t newsize = a->size * 2;
	if (newsize < a->length + n) newsize = a->length + n;
	return ci_arr__linearize(a, newsize);
}


/* ============================================================
 * Insert / remove
 * ============================================================ */

/* append at tail. returns 1 on success, 0 on OOM. */
int ci_arr_push(ci_array *a, ci_ptr val) {
	if (a->length == a->size) {
		if (!ci_arr_ensure_space(a, 1)) return 0;
	}
	/* TODO: ci_inc(val) when refcounting elements */
	a->data[ci_arr__idx(a, a->length)] = val;
	a->length++;
	return 1;
}

/* remove from tail. returns element or NULL if empty. */
ci_ptr ci_arr_pop(ci_array *a) {
	if (a->length == 0) return NULL;
	a->length--;
	/* TODO: caller is responsible for ci_dec if needed */
	return a->data[ci_arr__idx(a, a->length)];
}

/* prepend at head. returns 1 on success, 0 on OOM. */
int ci_arr_unshift(ci_array *a, ci_ptr val) {
	if (a->length == a->size) {
		if (!ci_arr_ensure_space(a, 1)) return 0;
	}
	/* TODO: ci_inc(val) when refcounting elements */
	a->offset = (a->offset == 0) ? a->size - 1 : a->offset - 1;
	a->data[a->offset] = val;
	a->length++;
	return 1;
}

/* remove from head. returns element or NULL if empty. */
ci_ptr ci_arr_shift(ci_array *a) {
	if (a->length == 0) return NULL;
	ci_ptr val = a->data[a->offset];
	/* TODO: caller is responsible for ci_dec if needed */
	a->offset = (a->offset + 1) % a->size;
	a->length--;
	return val;
}

/* extend array to at least `newlen` elements, filling new slots with NULL.
 * returns 1 on success, 0 on OOM. */
int ci_arr_extend(ci_array *a, uint32_t newlen) {
	if (newlen <= a->length)
		return 1;
	if (!ci_arr_ensure_space(a, newlen - a->length))
		return 0;
	ci_arr__zero_tail(a, a->length, newlen);
	a->length = newlen;
	return 1;
}

/* append all elements of src to dst. returns 1 on success, 0 on OOM.
 * safe to call with dst == src (self-append). */
int ci_arr_append(ci_array *dst, const ci_array *src) {
	uint32_t n = src->length; /* capture before possible self-append mutation */
	if (n == 0) return 1;
	if (!ci_arr_ensure_space(dst, n)) return 0;
	
	for (uint32_t i = 0; i < n; i++) {
		ci_ptr el = ci_arr(src, i);
		ci_inc(el);
		dst->data[ci_arr__idx(dst, dst->length)] = el;
	}
	dst->length += n;
	
	return 1;
}


/* ============================================================
 * Utility
 * ============================================================ */

void ci_arr_clear(ci_array *a) {
	if (!CI_ARR_IS_WRAPPED(a)) {
		ci_dec_multi(CI_ARR_UNWRAPPED_HEAD(a), CI_ARR_UNWRAPPED_LENGTH(a));
	} else {
		ci_dec_multi(CI_ARR_WRAPPED_SEG1_HEAD(a), CI_ARR_WRAPPED_SEG1_LENGTH(a));
		ci_dec_multi(CI_ARR_WRAPPED_SEG2_HEAD(a), CI_ARR_WRAPPED_SEG2_LENGTH(a));
	}
	
	a->length = 0;
	a->offset = 0;
}

void ci_arr_reverse(ci_array *a) {
	if (a->length <= 1) return;
	uint32_t lo = 0;
	uint32_t hi = a->length - 1;
	while (lo < hi) {
		uint32_t li = ci_arr__idx(a, lo);
		uint32_t ri = ci_arr__idx(a, hi);
		ci_ptr tmp  = a->data[li];
		a->data[li] = a->data[ri];
		a->data[ri] = tmp;
		lo++;
		hi--;
	}
}

/* returns logical index of first occurrence at or after start_idx, or -1. */
int32_t ci_arr_find(ci_array *a, ci_ptr element, uint32_t start_idx) {
	for (uint32_t i = start_idx; i < a->length; i++) {
		if (a->data[ci_arr__idx(a, i)] == element)
			return (int32_t)i;
	}
	return -1;
}

int ci_arr_contains(ci_array *a, ci_ptr element) {
	return ci_arr_find(a, element, 0) != -1;
}

/*
 * ci_arr_slice(a, from, to) — returns a new full array with elements [from, to).
 *
 * Index rules (same as Python slicing):
 *   - Range is half-open [from, to) — element at `to` is excluded.
 *   - Positive indices count from the start: 0 = first element.
 *   - Negative indices count from the end: -1 = last, -2 = second-to-last, etc.
 *   - Out-of-bounds positive indices are clamped to array length silently.
 *   - Out-of-bounds negative indices are clamped to 0 silently.
 *   - Pass INT32_MAX for `to` to slice to end of array (clamps to length).
 *   - Inverted range (to <= from after clamping) returns a new empty array.
 *
 * Cases:
 *   slice(a, 0,  INT32_MAX)  — full copy (same as ci_arr_copy)
 *   slice(a, n,  INT32_MAX)  — from index n to end
 *   slice(a, 0,  n)          — first n elements
 *   slice(a, -n, INT32_MAX)  — last n elements
 *   slice(a, 0,  -n)         — all except last n elements
 *   slice(a, -m, -n)         — from m-from-end to n-from-end (exclusive)
 *   slice(a, 2,  5)          — elements at indices 2, 3, 4
 *   slice(a, n,  n)          — empty (zero-length range)
 *   slice(a, 5,  2)          — empty (inverted, not an error)
 *   slice(a, 0,  9999)       — entire array (clamped)
 */
ci_array *ci_arr_slice(ci_array *a, int32_t from, int32_t to) {
	/* resolve from */
	uint32_t from_off;
	if (from < 0) {
		int32_t f = (int32_t)a->length + from;
		from_off = (f < 0) ? 0 : (uint32_t)f;
	} else {
		from_off = (uint32_t)from;
	}
	if (from_off > a->length) from_off = a->length;

	/* resolve to — positive values clamp to length, INT32_MAX naturally clamps too */
	uint32_t to_off;
	if (to < 0) {
		int32_t t = (int32_t)a->length + to;
		to_off = (t < 0) ? 0 : (uint32_t)t;
	} else {
		to_off = (uint32_t)to;
		if (to_off > a->length) to_off = a->length;
	}

	/* inverted range → empty array */
	if (to_off <= from_off) return ci_arr_new(CI_ARR_DEFAULT_CAP);

	uint32_t slice_len = to_off - from_off;

	ci_array *result = ci_arr_new(slice_len);
	if (!result) return NULL;

	for (uint32_t i = 0; i < slice_len; i++) {
		ci_ptr el = a->data[ci_arr__idx(a, from_off + i)];
		ci_inc(el);
		result->data[i] = el;
	}
		
	result->length = slice_len;
	return result;
}

ci_array *ci_arr_copy(ci_array *a) {
	return ci_arr_slice(a, 0, INT32_MAX);
}

void ci_array_dump(const ci_array *a) {
	printf("ci_array(%p) size=%u length=%u [", a, a->size, a->length);
	for (uint32_t i = 0; i < a->length; i++) {
		if (i) printf(", ");
		printf("%p", ci_arr_index(a, i));
	}
	printf("]\n");
}

/* element-wise pointer equality. returns 1 if equal, 0 otherwise. */
int ci_arr_eq(const ci_array *a, const ci_array *b) {
	if (a == b) return 1;
	if (a->length != b->length) return 0;
	for (uint32_t i = 0; i < a->length; i++) {
		if (ci_arr_index(a, i) != ci_arr_index(b, i)) return 0;
	}
	return 1;
}
