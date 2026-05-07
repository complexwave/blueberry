/*
 * tgmemlib.h — tagged arena allocator
 *
 * Each type gets a chain of 64 KiB arenas.
 *
 * Two mmap strategies (compile-time):
 *
 *   TG_ADDR_TAG (default if not defined as 0)
 *     Arenas are placed at computed addresses so the type tag is encoded
 *     in the pointer itself.  tg_ptr_tag() is a pure bitop — no memory read.
 *     Requires mmap address control.  Falls back to simple strategy at
 *     runtime if probing fails.
 *
 *   TG_ADDR_TAG=0  (-DTG_ADDR_TAG=0)
 *     Plain aligned mmap (2x-alloc-and-trim).  Tag is read from the arena
 *     header.  Works everywhere including bare-metal with pre-allocated blocks.
 *
 * Single-threaded only.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

/* ---- compile-time strategy selection ---- */

#ifndef TG_ADDR_TAG
#define TG_ADDR_TAG 1
#endif

/* ---- constants ---- */

#define ARENA_SIZE	(64u * 1024u)		/* bytes per arena, must be power of 2 */
#define ARENA_SHIFT	16			/* log2(ARENA_SIZE) */
#define PTR_TAG_BITS	6			/* bits embedded in pointer address */
#define PTR_MAX_TYPES	(1u << PTR_TAG_BITS)	/* 64: distinct pointer-level types */
#define MAX_TYPES	65536			/* full 16-bit tag space */
#define TAG_BITS	PTR_TAG_BITS		/* alias: used by STRIPE_SIZE */
#define STRIPE_SIZE	(1u << (ARENA_SHIFT + TAG_BITS))  /* 4 MiB */

/* ---- types ---- */

/* forward declare for use in function pointer signatures */
typedef struct tg_arena tg_arena_t;

/* destructor: called by tg_free before returning slot to freelist */
typedef void (*tg_type_destructor_fn)(void *ptr, tg_arena_t *arena);

/* visitor: called by tracing GC to walk references (future use) */
typedef void (*tg_type_visitor_fn)(void *ptr, tg_arena_t *arena);

/*
 * tg_arena_ops — per-arena type operations.
 * Copied (not pointed to) into each arena, so different arenas of
 * the same type tag can carry different ops if needed later.
 */
typedef struct {
	tg_type_destructor_fn destructor;
	tg_type_visitor_fn    visitor;
	
	union {
		void                 *ctx;
		void                 *prototype;
	};
} tg_arena_ops;

/*
 * tg_arena — slab header, lives at byte 0 of its mmap region.
 * Recover from any object pointer: (tg_arena_t *)(ptr & ARENA_MASK)
 */
struct tg_arena {
	uint16_t         type_tag;
	uint16_t         obj_size;
	uint16_t         live_count;	/* number of live objects */
	void            *freelist;	/* intrusive LIFO: first word of free slot = next */
	char            *bump;		/* next fresh slot */
	char            *end;		/* one-past-last byte of arena */
	struct tg_arena *next;		/* older arena of same type */
	tg_arena_ops     ops;		/* copied from allocator on arena creation */
};

/*
 * tg_mmap_strategy — function pointers for arena memory management.
 * Swappable at runtime (e.g. probe strategy fails -> fall back to simple).
 */
typedef struct tg_mmap_strategy {
	/* Allocate ARENA_SIZE bytes for the given tag.  Returns aligned ptr or NULL. */
	void *(*arena_mmap)(void *ctx, uint8_t tag);
	/* Free an arena's backing memory. */
	void  (*arena_munmap)(void *ctx, void *ptr);
	/* Destroy strategy context (called from tg_allocator_destroy). */
	void  (*destroy)(void *ctx);
	/* Opaque context (reservation base, etc). */
	void  *ctx;
} tg_mmap_strategy_t;

/* tg_allocator — top-level handle; owns all arenas */
typedef struct {
	tg_arena_t       **heads;		/* [MAX_TYPES] newest arena per tag; NULL = unregistered */
	uint16_t          *obj_sizes;		/* [MAX_TYPES] registered obj_size per tag */
	tg_arena_ops      *ops;			/* [MAX_TYPES] ops template; copied into each new arena */
	tg_mmap_strategy_t strategy;
} tg_allocator_t;

/* ---- internal layout macros ---- */

#define ARENA_HDR_SIZE	(((sizeof(tg_arena_t)) + 7u) & ~7u)	/* header size, 8-aligned */
#define ARENA_DATA(ar)	((char *)(ar) + ARENA_HDR_SIZE)		/* first object slot */
#define ARENA_MASK	(~((uintptr_t)ARENA_SIZE - 1))		/* mask to arena base */

/* ---- public API ---- */

/*
 * tg_allocator_new — create allocator.
 * Default strategy: probe-based address tagging (TG_ADDR_TAG=1)
 *                   or plain aligned mmap (TG_ADDR_TAG=0).
 * If probe fails at init, automatically falls back to plain mmap.
 * Returns NULL on failure.
 */
tg_allocator_t *tg_allocator_new(void);

/* tg_allocator_destroy — munmap all arenas, free the allocator */
void tg_allocator_destroy(tg_allocator_t *alloc);

/*
 * tg_allocator_register_type — bind tag to obj_size (rounded up to 8).
 * Allocates the first arena immediately so heads[tag] is always valid.
 * obj_size >= sizeof(void*) required (freelist stored inside free slots).
 * tag must be < MAX_TYPES.
 */
void tg_allocator_register_type(tg_allocator_t *alloc, uint16_t tag, uint16_t obj_size);

/*
 * tg_allocator_register_type_ops — like register_type, but copies ops into the
 * allocator's template for this tag.  Every arena created for this tag will
 * inherit the ops (destructor, visitor, ctx).
 */
void tg_allocator_register_type_ops(tg_allocator_t *alloc, uint16_t tag, uint16_t obj_size,
                                    const tg_arena_ops *ops);

/*
 * tg_allocator_new_arena — explicitly pre-alloc a fresh arena for tag.
 * Prepends to the chain; useful before a known burst of allocations.
 * Returns NULL on mmap failure.
 */
tg_arena_t *tg_allocator_new_arena(tg_allocator_t *alloc, uint16_t tag);

/*
 * tg_alloc — allocate one object of type tag.
 * Draws from freelist, then bump pointer; grows chain automatically on full.
 * Returns NULL on mmap failure.
 */
void *tg_alloc(tg_allocator_t *alloc, uint16_t tag);

/*
 * tg_free — return object to its arena's freelist.
 * No allocator pointer needed; arena recovered from ptr via ARENA_MASK.
 */
void tg_free(void *ptr);

/*
 * tg_alloc_linked — allocate `count` contiguous object slots.
 * Sorts freelist to find adjacent free slots; falls back to bump pointer,
 * then to a new arena.  Returns pointer to first slot, or NULL on failure.
 * Caller manages the multi-slot lifetime (typically via destructor).
 */
void *tg_alloc_linked(tg_allocator_t *alloc, uint16_t tag, int count);

/*
 * TG_SLOT_COUNT(byte_size, obj_size) — round up byte_size to number of slots.
 */
#define TG_SLOT_COUNT(byte_size, obj_size) \
	(((byte_size) + (obj_size) - 1) / (obj_size))

/*
 * tg_free_linked — return extra linked slots from a destructor.
 * Takes total byte size of the extended struct (e.g. sizeof(my_big_struct)).
 * Frees (slots - 1) starting at ptr + obj_size — the first slot is already
 * being freed by the tg_free() that called the destructor.
 * No destructors called.
 */
void tg_free_linked(void *ptr, size_t byte_size);

/*
 * tg_cleanup — free empty arenas for a given type.
 * Always keeps at least one arena. Returns number of arenas freed.
 */
int tg_cleanup(tg_allocator_t *alloc, uint16_t tag);

/* ---- strategy constructors ---- */

/* Pool: reserve pool_size virtual bytes (PROT_NONE), commit per-arena on alloc
 * (MAP_FIXED PROT_RW), decommit on free (MAP_FIXED PROT_NONE).
 * pool_size=0 uses 1 GiB default.  Only one mmap reservation at init. */
tg_mmap_strategy_t tg_strategy_pool(size_t pool_size);

/* Plain 2x-alloc-and-trim aligned mmap.  No address tagging. */
tg_mmap_strategy_t tg_strategy_simple(void);

#if TG_ADDR_TAG
/* Probe-based: alloc+free a page to find base, then mmap at tag-encoded addrs.
 * Returns simple strategy on probe failure. */
tg_mmap_strategy_t tg_strategy_probe(void);

/* Reserve-then-commit: PROT_NONE reservation, MAP_FIXED commit.
 * max_stripes: how many stripes to reserve (each = 4 MiB virtual).
 * Returns simple strategy on reservation failure. */
tg_mmap_strategy_t tg_strategy_reserve(int max_stripes);
#endif

/* ---- inline helpers ---- */

/* tg_ptr_arena — arena header from any object pointer, one bitop */
static inline tg_arena_t *tg_ptr_arena(void *ptr) {
	return (tg_arena_t *)((uintptr_t)ptr & ARENA_MASK);
}

/*
 * tg_ptr_tag — type tag from object pointer.
 *
 * TG_ADDR_TAG=1 (default): pure bitop, no memory access.
 *   NOTE: only valid if allocator was created with an addr-tag strategy
 *   (probe or reserve) AND that strategy didn't fall back.
 *   If you might have fallen back, use tg_ptr_tag_arena() instead.
 *
 * TG_ADDR_TAG=0: reads tag from arena header (always correct).
 */
#if TG_ADDR_TAG
static inline uint8_t tg_ptr_tag(void *ptr) {
	return (uint8_t)(((uintptr_t)ptr >> ARENA_SHIFT) & (MAX_TYPES - 1));
}
#else
static inline uint8_t tg_ptr_tag(void *ptr) {
	return tg_ptr_arena(ptr)->type_tag;
}
#endif

static inline uint16_t tg_ptr_size(void *ptr) {
	return tg_ptr_arena(ptr)->obj_size;
}

/* tg_ptr_tag_arena — always reads ptrtag from arena header (safe fallback) */
static inline uint8_t tg_ptr_tag_arena(void *ptr) {
	return (uint8_t)(tg_ptr_arena(ptr)->type_tag & (PTR_MAX_TYPES - 1));
}

/* tg_ptr_tag_full — reads full 16-bit tag from arena header */
static inline uint16_t tg_ptr_tag_full(void *ptr) {
	return tg_ptr_arena(ptr)->type_tag;
}

/* tg_arena_capacity — max objects that fit in one arena of this type */
static inline int tg_arena_capacity(tg_arena_t *ar) {
	return (int)((ar->end - ARENA_DATA(ar)) / ar->obj_size);
}

/* tg_arena_count — number of arenas in chain for a type */
static inline int tg_arena_count(tg_allocator_t *alloc, uint16_t tag) {
	int n = 0;
	for (tg_arena_t *ar = alloc->heads[tag]; ar; ar = ar->next) n++;
	return n;
}

/* tg_allocator_addr_tagged — check if allocator is using address tagging */
static inline int tg_allocator_addr_tagged(tg_allocator_t *alloc) {
#if TG_ADDR_TAG
	/* if ctx is non-NULL, an addr-tag strategy is active */
	return alloc->strategy.ctx != NULL;
#else
	(void)alloc;
	return 0;
#endif
}
