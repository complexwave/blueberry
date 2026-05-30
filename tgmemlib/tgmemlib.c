/*
 * tgmemlib.c — implementation
 * Include this file directly; no separate compilation needed.
 */

#include "tgmemlib.h"
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <assert.h>

/* ==== Strategies ==== */

#include "tgmemlib_strategy_simple.c"
#include "tgmemlib_strategy_pool.c"

#if TG_ADDR_TAG
#include "tgmemlib_strategy_probe.c"
#include "tgmemlib_strategy_reserve.c"
#endif

#ifdef TGMEMLIB_TRACKING
/* global pointer so tg_free (which has no allocator arg) can bump the counter */
tg_allocator_t *tg_track_allocator;
#endif

/* ==== Arena internals ==== */

static tg_arena_t *tg_arena_new(tg_allocator_t *alloc, uint16_t tag, uint16_t obj_size) {
	uint8_t ptrtag = (uint8_t)(tag & (PTR_MAX_TYPES - 1));
	void *mem = alloc->strategy.arena_mmap(alloc->strategy.ctx, ptrtag);
	if (!mem) return NULL;

	tg_arena_t *ar = (tg_arena_t *)mem;
	ar->type_tag   = tag;
	ar->obj_size   = obj_size;
	ar->live_count = 0;
	ar->freelist   = NULL;
	ar->bump       = ARENA_DATA(ar);
	ar->end        = (char *)mem + ARENA_SIZE;
	ar->next       = NULL;
	ar->ops        = alloc->ops[tag];

	/* poison all data slots — unpoison individually on alloc */
	TG_ASAN_POISON(ARENA_DATA(ar), (size_t)(ar->end - ARENA_DATA(ar)));

	return ar;
}

static void tg_arena_destroy(tg_allocator_t *alloc, tg_arena_t *ar) {
	alloc->strategy.arena_munmap(alloc->strategy.ctx, ar);
}

/* returns NULL when freelist empty and bump exhausted */
static void *tg_arena_alloc(tg_arena_t *ar) {
	void *obj;

#ifdef TG_ASAN
	/* ASan mode: bump only, never reuse freed slots */
	if (ar->bump + ar->obj_size <= ar->end) {
		obj       = ar->bump;
		ar->bump += ar->obj_size;
	} else {
		return NULL;
	}
#else
	if (ar->freelist) {
		obj          = ar->freelist;
		ar->freelist = *(void **)obj;
	} else if (ar->bump + ar->obj_size <= ar->end) {
		obj       = ar->bump;
		ar->bump += ar->obj_size;
	} else {
		return NULL;
	}
#endif

	TG_ASAN_UNPOISON(obj, ar->obj_size);
	ar->live_count++;
	return obj;
}

/* ==== Linked (contiguous multi-slot) allocation ==== */

/*
 * Scan sorted freelist for a contiguous run of `need` slots.
 * If found: unlink them, bump live_count, return pointer to first slot.
 */
static void *tg_freelist_find_run(tg_arena_t *ar, int need) {
	void **prev_start = &ar->freelist;   /* ->next of node before run start */
	void  *start      = ar->freelist;
	void  *p          = start;
	int    seq        = 1;

	while (p && *(void **)p) {
		void *nx = *(void **)p;
		if ((char *)p + ar->obj_size == (char *)nx) {
			seq++;
			if (seq >= need) {
				/* unlink: skip `need` nodes from prev_start */
				void *tail = start;
				for (int i = 0; i < need; i++)
					tail = *(void **)tail;
				*prev_start = tail;
				ar->live_count += need;
				return start;
			}
		} else {
			prev_start = (void **)p;
			start      = nx;
			seq        = 1;
		}
		p = nx;
	}
	return NULL;
}

/*
 * Bubble-sort the freelist by address, checking for a contiguous run
 * of `need` slots after each pass.  Exits early once found or fully sorted.
 */
static void *tg_arena_bubblesort_freelist(tg_arena_t *ar, int need) {
	if (!ar->freelist) return NULL;

	for (;;) {
		void **prev_next = &ar->freelist;
		void  *cur       = ar->freelist;
		int    swapped   = 0;

		while (cur && *(void **)cur) {
			void *nxt = *(void **)cur;
			if ((uintptr_t)cur > (uintptr_t)nxt) {
				*(void **)cur  = *(void **)nxt;
				*(void **)nxt  = cur;
				*prev_next     = nxt;
				prev_next      = (void **)nxt;
				swapped        = 1;
			} else {
				prev_next = (void **)cur;
				cur       = nxt;
			}
		}

		/* scan for run after this pass */
		void *found = tg_freelist_find_run(ar, need);
		if (found) return found;

		if (!swapped) break;   /* fully sorted, no run exists */
	}

	return NULL;
}

/*
 * tg_arena_alloc_linked — allocate `count` contiguous object slots.
 * Tries freelist sort first (to exercise that path), then bump.
 */
static void *tg_arena_alloc_linked(tg_arena_t *ar, int count) {
	size_t total = (size_t)count * ar->obj_size;

#ifndef TG_ASAN
	void *ptr = tg_arena_bubblesort_freelist(ar, count);
	if (ptr) {
		TG_ASAN_UNPOISON(ptr, total);
		return ptr;
	}
#endif

	void *ptr;
	if (ar->bump + total <= ar->end) {
		ptr = ar->bump;
		ar->bump += total;
		ar->live_count += count;
		TG_ASAN_UNPOISON(ptr, total);
		return ptr;
	}

	return NULL;
}

/* ==== Public API ==== */

tg_allocator_t *tg_allocator_new(void) {
	tg_allocator_t *alloc = calloc(1, sizeof(*alloc));
	if (!alloc) return NULL;

	alloc->heads     = calloc(MAX_TYPES, sizeof(*alloc->heads));
	alloc->obj_sizes = calloc(MAX_TYPES, sizeof(*alloc->obj_sizes));
	alloc->ops       = calloc(MAX_TYPES, sizeof(*alloc->ops));
	
	if (!alloc->heads || !alloc->obj_sizes || !alloc->ops) {
		free(alloc->heads);
		free(alloc->obj_sizes);
		free(alloc->ops);
		free(alloc);
		return NULL;
	}

	alloc->strategy = tg_strategy_probe();  /* addr-tag: tag bits baked into pointer address */

#ifdef TGMEMLIB_TRACKING
	tg_track_allocator = alloc;
#endif
	return alloc;
}

void tg_allocator_destroy(tg_allocator_t *alloc) {
	for (int i = 0; i < MAX_TYPES; i++) {
		tg_arena_t *ar = alloc->heads[i];
		
		while (ar) {
			tg_arena_t *next = ar->next;
			tg_arena_destroy(alloc, ar);
			ar = next;
		}
	}
	
	if (alloc->strategy.destroy)
		alloc->strategy.destroy(alloc->strategy.ctx);
	
	free(alloc->newalloc);
	free(alloc->heads);
	free(alloc->obj_sizes);
	free(alloc->ops);
	free(alloc);
}

void tg_allocator_register_type(tg_allocator_t *alloc, uint16_t tag, uint16_t obj_size) {
	assert(obj_size >= sizeof(void *));

	obj_size = (uint16_t)((obj_size + 7u) & ~7u);
	alloc->obj_sizes[tag] = obj_size;
	
	memset(&alloc->ops[tag], 0, sizeof(tg_arena_ops));

	tg_arena_t *ar = tg_arena_new(alloc, tag, obj_size);
	assert(ar && "first arena mmap failed");

	ar->next          = alloc->heads[tag];
	alloc->heads[tag] = ar;
}

void tg_allocator_register_type_ops(tg_allocator_t *alloc, uint16_t tag, uint16_t obj_size,
                                    const tg_arena_ops *ops) {
	assert(obj_size >= sizeof(void *));
	assert(ops);

	obj_size = (uint16_t)((obj_size + 7u) & ~7u);
	alloc->obj_sizes[tag] = obj_size;
	alloc->ops[tag] = *ops;

	tg_arena_t *ar = tg_arena_new(alloc, tag, obj_size);
	assert(ar && "first arena mmap failed");

	ar->next          = alloc->heads[tag];
	alloc->heads[tag] = ar;
}

tg_arena_t *tg_allocator_new_arena(tg_allocator_t *alloc, uint16_t tag) {
	assert(alloc->heads[tag] && "type not registered");

	tg_arena_t *ar = tg_arena_new(alloc, tag, alloc->obj_sizes[tag]);
	if (!ar) return NULL;
	
	ar->next          = alloc->heads[tag];
	alloc->heads[tag] = ar;
	
	return ar;
}

/* append to newalloc stack; trigger cleanup at low watermark */
static inline void tg_newalloc_push(tg_allocator_t *alloc, void *obj) {
	if (!alloc->newalloc) return;

	alloc->newalloc[alloc->newalloc_pos++] = obj;

	if (alloc->newalloc_pos >= alloc->newalloc_lo) {
		tg_newalloc_cleanup(alloc);
	}
}

void *tg_alloc(tg_allocator_t *alloc, uint16_t tag) {
	void *obj = tg_arena_alloc(alloc->heads[tag]);
	if (obj) {
#ifdef TGMEMLIB_TRACKING
		alloc->track_alloc_total++;
#endif
		tg_newalloc_push(alloc, obj);
		return obj;
	}

	tg_arena_t *ar = tg_allocator_new_arena(alloc, tag);
	if (!ar) return NULL;

	obj = tg_arena_alloc(ar);
	if (obj) {
#ifdef TGMEMLIB_TRACKING
		alloc->track_alloc_total++;
#endif
		tg_newalloc_push(alloc, obj);
	}
	return obj;
}

void tg_free(void *ptr) {
	tg_arena_t *ar = tg_ptr_arena(ptr);

	if (ar->ops.destructor) {
		ar->ops.destructor(ptr, ar);
	}

#ifdef TG_ASAN
	/* poison the slot — any later access will trap */
	TG_ASAN_POISON(ptr, ar->obj_size);
#else
	*(void **)ptr  = ar->freelist;
	ar->freelist   = ptr;
#endif
	ar->live_count--;

#ifdef TGMEMLIB_TRACKING
	if (tg_track_allocator) tg_track_allocator->track_free_total++;
#endif
}

void *tg_alloc_linked(tg_allocator_t *alloc, uint16_t tag, size_t byte_size) {
	int count = TG_SLOT_COUNT(byte_size, alloc->obj_sizes[tag]);
	assert(count >= 1);

	void *ptr;

	/* try existing head arena */
	ptr = tg_arena_alloc_linked(alloc->heads[tag], count);
	if (ptr) { tg_newalloc_push(alloc, ptr); return ptr; }

	/* walk older arenas */
	for (tg_arena_t *ar = alloc->heads[tag]->next; ar; ar = ar->next) {
		ptr = tg_arena_alloc_linked(ar, count);
		if (ptr) { tg_newalloc_push(alloc, ptr); return ptr; }
	}

	/* new arena — bump always has contiguous space */
	tg_arena_t *ar = tg_allocator_new_arena(alloc, tag);
	if (!ar) return NULL;

	ptr = tg_arena_alloc_linked(ar, count);
	if (ptr) tg_newalloc_push(alloc, ptr);
	return ptr;
}

void tg_free_linked(void *ptr, size_t byte_size) {
	tg_arena_t *ar = tg_ptr_arena(ptr);
	int extra = TG_SLOT_COUNT(byte_size, ar->obj_size) - 1;
	char *slot = (char *)ptr + ar->obj_size;

	for (int i = 0; i < extra; i++) {
#ifdef TG_ASAN
		TG_ASAN_POISON(slot, ar->obj_size);
#else
		*(void **)slot = ar->freelist;
		ar->freelist   = slot;
#endif
		ar->live_count--;
		slot += ar->obj_size;
	}
}

/* ==== Newalloc stack ==== */

void tg_newalloc_resize(tg_allocator_t *alloc, int count) {
	free(alloc->newalloc);

	alloc->newalloc      = malloc((size_t)count * sizeof(void *));
	alloc->newalloc_size = count;
	alloc->newalloc_pos  = 0;
	alloc->newalloc_lo   = (int)(count * 0.70);
	alloc->newalloc_hi   = (int)(count * 0.90);
}

/* default cleanup: just drain the stack.
 * define TG_NEWALLOC_CLEANUP_OVERRIDE before including to provide your own. */
#ifndef TG_NEWALLOC_CLEANUP_OVERRIDE
void tg_newalloc_cleanup(tg_allocator_t *alloc) {
	alloc->newalloc_pos = 0;
}
#endif

/* ==== Debug / introspection ==== */

#include "tgmemlib_debug.c"

/* ==== Cleanup ==== */

int tg_cleanup(tg_allocator_t *alloc, uint16_t tag) {
	tg_arena_t *ar = alloc->heads[tag];
	if (!ar) return 0;

	int freed = 0;
	tg_arena_t *prev = ar;
	tg_arena_t *cur  = ar->next;
	while (cur) {
		tg_arena_t *next = cur->next;
		
		if (cur->live_count == 0) {
			prev->next = next;
			tg_arena_destroy(alloc, cur);
			freed++;
		} else {
			prev = cur;
		}
		
		cur = next;
	}
	
	if (ar->live_count == 0 && ar->next) {
		alloc->heads[tag] = ar->next;
		tg_arena_destroy(alloc, ar);
		freed++;
	}
	return freed;
}
