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
	return ar;
}

static void tg_arena_destroy(tg_allocator_t *alloc, tg_arena_t *ar) {
	alloc->strategy.arena_munmap(alloc->strategy.ctx, ar);
}

/* returns NULL when freelist empty and bump exhausted */
static void *tg_arena_alloc(tg_arena_t *ar) {
	void *obj;
	if (ar->freelist) {
		obj          = ar->freelist;
		ar->freelist = *(void **)obj;
	} else if (ar->bump + ar->obj_size <= ar->end) {
		obj       = ar->bump;
		ar->bump += ar->obj_size;
	} else {
		return NULL;
	}
	ar->live_count++;
	return obj;
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

void *tg_alloc(tg_allocator_t *alloc, uint16_t tag) {
	void *obj = tg_arena_alloc(alloc->heads[tag]);
	if (obj) return obj;

	tg_arena_t *ar = tg_allocator_new_arena(alloc, tag);
	if (!ar) return NULL;
	return tg_arena_alloc(ar);
}

void tg_free(void *ptr) {
	tg_arena_t *ar = tg_ptr_arena(ptr);

	if (ar->ops.destructor) {
		ar->ops.destructor(ptr, ar);
	}

	*(void **)ptr  = ar->freelist;
	ar->freelist   = ptr;
	ar->live_count--;
}

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
