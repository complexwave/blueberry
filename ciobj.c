/*
 * ciobj.c — Citrin object system implementation + tests
 */

#include "tgmemlib/tgmemlib.c"
#include "ciobj.h"
#include <stdio.h>

/* ---- global allocator ---- */

tg_allocator_t *ci_alloc;

void ci_init(void) {
	ci_alloc = tg_allocator_new();
	assert(ci_alloc && "ci_init: allocator creation failed");
}

void ci_shutdown(void) {
	tg_allocator_destroy(ci_alloc);
	ci_alloc = NULL;
}

void ci_register(uint16_t tag, uint16_t obj_size) {
	tg_allocator_register_type(ci_alloc, tag, obj_size);
}

void ci_register_ops(uint16_t tag, uint16_t obj_size, const tg_arena_ops *ops) {
	tg_allocator_register_type_ops(ci_alloc, tag, obj_size, ops);
}

#include "ci_string.c"
#include "ci_array.c"
#ifndef CIMAP_INCLUDE
#define CIMAP_INCLUDE "ci_map.c"
#endif
#include CIMAP_INCLUDE

/* ==== test ==== */

/* test types */
#define CI_TEST_OBJ_RC   CI_TAG(0,0,0,0,0,0,0,0, 0,0,1,1,1,1,1,0)  /* ptrtag=0x3E: refcountable user obj */
#define CI_TEST_OBJ_NORC CI_TAG(0,0,0,0,0,0,0,0, 0,0,1,1,1,1,0,0)  /* ptrtag=0x3C: non-refcountable user obj */

typedef struct {
	CI_GC_HDR;
	char mydata[120];
} my_ci_object;

#if !defined(CI_STRING_TEST) && !defined(CI_ARRAY_TEST) && !defined(CI_MAP_TEST)
int main(void) {
	ci_init();

	ci_register(CI_TEST_OBJ_RC,   sizeof(my_ci_object));
	ci_register(CI_TEST_OBJ_NORC, sizeof(my_ci_object));

	/* -- alloc -- */
	my_ci_object *o1 = ci_new(CI_TEST_OBJ_RC);
	my_ci_object *o2 = ci_new(CI_TEST_OBJ_NORC);
	assert(o1 && o2);

	/* o1 is refcountable, starts at 1 */
	assert(ci_refcnt(o1) == 1);
	assert(ci_is_refcountable(o1));

	/* o2 is not refcountable, refcnt untouched (0 from alloc) */
	assert(!ci_is_refcountable(o2));

	/* -- inc -- */
	ci_inc(o1);
	assert(ci_refcnt(o1) == 2);

	ci_inc(o2);  /* no-op on non-refcountable */

	/* -- dec -- */
	assert(ci_dec(o1) == 0);  /* 2 -> 1, not freed */
	assert(ci_refcnt(o1) == 1);

	ci_dec(o2);  /* no-op */

	assert(ci_dec(o1) == 1);  /* 1 -> 0, freed */
	/* o1 is now freed, don't touch it */

	ci_free(o2);  /* manual free for non-refcounted */

	/* -- saturation test -- */
	my_ci_object *o3 = ci_new(CI_TEST_OBJ_RC);
	assert(o3);
	((ci_gchdr *)o3)->refcnt = 0xFFFE;
	ci_inc(o3);
	assert(ci_refcnt(o3) == 0xFFFF);  /* saturated */
	ci_inc(o3);
	assert(ci_refcnt(o3) == 0xFFFF);  /* stays saturated */
	assert(ci_dec(o3) == 0);          /* saturated: no decrement, no free */
	assert(ci_refcnt(o3) == 0xFFFF);
	ci_free(o3);  /* force free to clean up */

	ci_shutdown();

	printf("ciobj: all tests passed\n");
	return 0;
}
#endif /* CI_STRING_TEST || CI_ARRAY_TEST || CI_MAP_TEST */
