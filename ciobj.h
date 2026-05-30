/*
 * ciobj.h — Citrin object system
 *
 * GC object layer on top of tgmemlib tagged allocator.
 * Single-threaded, manual refcounting with saturating u16.
 *
 * Ptrtag layout (6 bits embedded in pointer address at ARENA_SHIFT):
 *
 *   bit 5  bit 4  bit 3  bit 2  bit 1  bit 0
 *   [─── family field (4 bits, CI_O_FAMILY_BIT_OFFSET=2) ───]  RC  RO
 *
 *   bit 0  (CI_TAG_READONLY): 1 = immutable object
 *   bit 1  (CI_REFCOUNTABLE): 1 = manually refcounted
 *   bits 2-5: family field — lowest set bit is the family marker.
 *             Higher bits encode the entry index within the family.
 *
 *   CI_FAMILY_ENTRY(f, e) = (((e<<1)|1) * f)
 *   CI_FAMILY_MASK(f)     = all bits at f's level and above (within field)
 *
 *   ptrtag bits   family          entries  constant   used by
 *   ──────────────────────────────────────────────────────────
 *   xxx1          CI_O_FAMILY_8     8      0x04       vm objects
 *   xx10          CI_O_FAMILY_4     4      0x08       strings
 *   x100          CI_O_FAMILY_2     2      0x10       maps
 *   1000          CI_O_FAMILY_1     1      0x20       arrays
 *
 *   vm objects  f=0x04: e0=0x04  e1=0x0C  e2=0x14  e3=0x1C
 *                       e4=0x24  e5=0x2C  e6=0x34  e7=0x3C
 *   strings     f=0x08: e0=0x08  e1=0x18  e2=0x28  e3=0x38
 *   maps        f=0x10: e0=0x10  e1=0x30
 *   arrays      f=0x20: e0=0x20
 *
 *   Full tag = CI_FAMILY_ENTRY(f,e) | CI_REFCOUNTABLE (caller's job)
 *   No two families can collide — structurally impossible.
 */
#pragma once

#include "tgmemlib/tgmemlib.h"
#include <assert.h>
#include <unistd.h>

/* ---- tag bit definitions ---- */

typedef void* ci_ptr;

#define CI_TAG_READONLY  1

#define CI_REFCOUNTABLE  (1 << 1)

/* ---- family field macros ---- */

#define CI_IS_INT_BIT  (1 << 0)

//offset in the tag
#define CI_O_FAMILY_BIT_OFFSET  2
#define CI_O_FAMILY_BITS        4
#define CI_FAMILY_BITMASK       ( ((1 << CI_O_FAMILY_BITS)-1)<< CI_O_FAMILY_BIT_OFFSET )

#define CI_O_FAMILY_8   (1 << (CI_O_FAMILY_BIT_OFFSET + 0))  /* 0x04 — 8 entries */
#define CI_O_FAMILY_4   (1 << (CI_O_FAMILY_BIT_OFFSET + 1))  /* 0x08 — 4 entries */
#define CI_O_FAMILY_2   (1 << (CI_O_FAMILY_BIT_OFFSET + 2))  /* 0x10 — 2 entries */
#define CI_O_FAMILY_1   (1 << (CI_O_FAMILY_BIT_OFFSET + 3))  /* 0x20 — 1 entry  */

#define CI_TAGBITS(t) ((uintptr_t)(t) << ARENA_SHIFT)

/* all family bits at f's level and above (within field) */
#define CI_FAMILY_MASK(f)       CI_TAGBITS( CI_FAMILY_BITMASK & ~((uintptr_t)(f) - 1))

// bitmask with 1 for bits that dont belong to this family
#define CI_INV_FAMILY_MASK(f)   ((~CI_FAMILY_MASK(f)) & CI_FAMILY_BITMASK )

/* marker bit of family f — same as f itself */
#define CI_FAMILY_TAG(f)        (f)

/* tag for entry e within family f: (((e<<1)|1) * f) */
#define CI_FAMILY_ENTRY(f, e)   ((((uintptr_t)(e) << 1) | 1) * (uintptr_t)(f))

/* extract ptrtag from pointer */
#define CI_PTRTAG(ptr)          ((uintptr_t)(ptr) >> ARENA_SHIFT)

/*
 * CI_CHECK_MASK_FAMILY(ptr, mask, f)
 *   All bits in mask are set in ptrtag, AND no bits above mask within
 *   the family range are set.  Used to check exact family membership
 *   and sub-type bits without false positives from other families.
 */


#define CI_CHECK_MASK_FAMILY(ptr, mask, f) \
	( ((uintptr_t)(ptr) & (CI_TAGBITS(CI_INV_FAMILY_MASK(f) | mask) | CI_IS_INT_BIT )) == CI_TAGBITS(mask) )

/* family membership — marker bit set anywhere in ptrtag */
#define CI_IS_FAMILY(ptr, f)    CI_CHECK_MASK_FAMILY(ptr, (f), (f))

/* ---- GC header flag bits (ci_gchdr.flags) ---- */

/* Object data is stored inline in its arena slot (e.g. ci_str_small).
 * Shared convention across strings, arrays, maps, etc. */
#define CI_OBJ_SMALL    (1 << 0)

#define CI_IS_SMALL(p)    (((const ci_gchdr *)(p))->flags & CI_OBJ_SMALL)

/* Object is interned/readonly — cannot be mutated or upgraded.
 * Stored in gc.flags (not ptrtag) so it applies to any object type.
 * Used for dynamic promote/demote of readonly status. */
#define CI_OBJ_READONLY (1 << 8)

#define CI_IS_READONLY(p) (((const ci_gchdr *)(p))->flags & CI_OBJ_READONLY)


// simple pointer tagging
// 1 in lowest bit means this is integer

#define CI_INT_BITS       ((int)(sizeof(intptr_t) * 8 - 1))
#define CI_INT_MAX        ((intptr_t)((uintptr_t)1 << (CI_INT_BITS - 1)) - 1)
#define CI_INT_MIN        ((intptr_t)(-(intptr_t)((uintptr_t)1 << (CI_INT_BITS - 1))))

#define CI_IS_INT(p)  ((uintptr_t)(p) & 0x01)
#define CI_INT(p)  ((intptr_t)(p) >> 1)
#define CI_PACKINT(p)  ((void*)( ((intptr_t)(p) << 1) | 0x01))

// lower 3 bits assumed to be 0 for pointers
// x10 -- bool type, x - value of bool
// 001 - int(0) - false
// 010 - false
// 110 - true

#define CI_BOOL(v)      (ci_ptr)(uintptr_t)( ((!!(uintptr_t)(v)) << 2) | 0x02 )
#define CI_BOOL_INV(v)  (ci_ptr)(uintptr_t)( ((uintptr_t)(v)) ^ 0x04 )

#define CI_IS_FALSY(p)  ( (uintptr_t)(p) <= 2 )

#define CI_IS_BOOL(p)  ( ((uintptr_t)(p) & 0x03) == 0x02)

#define CI_IS_PTR(p)  ( p && !((uintptr_t)(p) & 0x03) )

/*
 * CI_UPPER_TAG(v) — shift value v into upper byte of tag
 */
#define CI_UPPER_TAG(v)  ((uint16_t)((v) << 8))

/*
 * CI_TAG(u7..u0, l7..l0) — build a 16-bit type tag.
 *   upper byte (u7..u0): extra type identity, not reflected in pointer.
 *   lower byte (l7..l0): l5..l0 are embedded in pointer address (ptrtag).
 *                        l7..l6 are in lower byte but not in pointer.
 * ptrtag = tag & 0x3F  (always, by construction)
 */
#define CI_TAG(u7,u6,u5,u4,u3,u2,u1,u0, l7,l6,l5,l4,l3,l2,l1,l0) \
	((uint16_t)( \
		((u7)<<15)|((u6)<<14)|((u5)<<13)|((u4)<<12)|((u3)<<11)|((u2)<<10)|((u1)<<9)|((u0)<<8) | \
		((l7)<<7) |((l6)<<6) |((l5)<<5) |((l4)<<4) |((l3)<<3) |((l2)<<2) |((l1)<<1)|(l0) \
	))

/* ci_tag_mask(a)       — shift bit mask into pointer tag position    */
/* CI_CHECK_MASK(ptr,a) — all bits in mask a are set in ptr's tag     */
#define CI_TAG_MASK(a)         ((uintptr_t)(a) << ARENA_SHIFT)
#define CI_CHECK_MASK(ptr, a)  (((uintptr_t)(ptr) & CI_TAG_MASK(a)) == CI_TAG_MASK(a))

/*
 * CI_AND_TAG(ptr, mask) — test tag bits directly in the pointer.
 *
 * TG_ADDR_TAG=1: shifts mask up to ARENA_SHIFT and ANDs the raw pointer.
 *   The shifted mask is a compile-time constant, so this is one AND instruction.
 *   Result is nonzero/zero — use in boolean context, not for extracting the tag value.
 *
 * TG_ADDR_TAG=0: falls back to extracting tag from arena header, then AND.
 */
#if TG_ADDR_TAG
#define CI_AND_TAG(ptr, mask) \
	((uintptr_t)(ptr) & ((uintptr_t)(mask) << ARENA_SHIFT))
#else
#define CI_AND_TAG(ptr, mask) \
	((uintptr_t)(tg_ptr_tag(ptr) & (mask)))
#endif

	
#define CI_IS_REFCOUNTABLE(ptr) (CI_IS_PTR(ptr) && CI_AND_TAG(ptr, CI_REFCOUNTABLE) != 0)
#define CI_IS_TAG_READONLY(ptr) CI_AND_TAG(ptr, CI_TAG_READONLY)

/* ---- GC header ---- */

typedef struct {
	uint16_t refcnt;
	uint16_t flags;
} ci_gchdr;

// upper 8: gc flags (CI_OBJ_READONLY etc.)
// lower 8: user flags (per-type: CI_OBJ_SMALL, CI_OBJ_SLICE, CI_TIMER_*, etc.)

/* embed in structs */
#define CI_GC_HDR  ci_gchdr gc

/* ---- global allocator ---- */

extern tg_allocator_t *ci_alloc;

void ci_init(void);
void ci_shutdown(void);
void ci_register(uint16_t tag, uint16_t obj_size);
void ci_register_ops(uint16_t tag, uint16_t obj_size, const tg_arena_ops *ops);

/* ---- allocation ---- */

static inline void *ci_new(uint16_t tag) {
	void *obj = tg_alloc(ci_alloc, tag);
	if (!obj) return NULL;

	if (tag & CI_REFCOUNTABLE) {
		ci_gchdr *hdr = (ci_gchdr *)obj;
		hdr->refcnt = 1;
		hdr->flags  = 0;
	}
	return obj;
}

/* manual free — for non-refcounted objects or forced cleanup */
static inline void ci_free(void *ptr) {
	tg_free(ptr);
}

/* ---- saturating refcount ---- */

static inline void ci_nocnt(void *ptr) {
	if (!CI_IS_REFCOUNTABLE(ptr)) return;

	ci_gchdr *hdr = (ci_gchdr *)ptr;
	hdr->refcnt = 0xFFFF;
}

#ifdef CI_DISABLE_REFCOUNTING

#define ci_inc(x) ((void)(x))
#define ci_dec(x) ((void)(x))

#else

static inline void ci_inc(void *ptr) {
	if (!CI_IS_REFCOUNTABLE(ptr)) return;

	ci_gchdr *hdr = (ci_gchdr *)ptr;
	uint16_t rc = hdr->refcnt + 1;
	rc |= -(uint16_t)(rc == 0);   /* overflow -> 0xFFFF (sticky) */
	hdr->refcnt = rc;
}

/* returns 1 if object was freed, 0 otherwise */
static inline int ci_dec(void *ptr) {
	if (!CI_IS_REFCOUNTABLE(ptr)) return 0;

	ci_gchdr *hdr = (ci_gchdr *)ptr;
	if (hdr->refcnt == 0xFFFF) return 0;  /* saturated, don't touch */

	hdr->refcnt--;
	if (hdr->refcnt == 0) {
		tg_free(ptr);
		return 1;
	}
	return 0;
}

#endif /* CI_DISABLE_REFCOUNTING */

#define ci_dec_multi(arr, n) do { \
	ci_ptr *_p = (arr); size_t _n = (n); \
	while (_n--) { ci_dec(*_p); _p++; } \
} while(0)

/* ---- query helpers ---- */

static inline uint16_t ci_refcnt(void *ptr) {
	return ((ci_gchdr *)ptr)->refcnt;
}

static inline int ci_is_refcountable(void *ptr) {
	return CI_IS_REFCOUNTABLE(ptr);
}

/* ---- warn event ---- */

static inline void ci_warn_event(ci_ptr ctx, const char *msg) {
	(void)ctx;
	write(2, msg, strlen(msg));
	write(2, "\n", 1);
}
