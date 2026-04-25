/* test_set_index.c — random access edge cases */
#include "ciobj.c"
#include <assert.h>
#include <stdio.h>

static void setup(void)    { ci_init(); ci_str_register(); ci_arr_register(); }
static void teardown(void) { ci_shutdown(); }

#define IPTR(i) ((ci_ptr)(uintptr_t)((uintptr_t)(i) * 8 + 8))

int main(void) {
	setup();

	/* --- Set at index 0 and len-1 (boundaries) --- */
	{
		ci_array *a = ci_arr_new(8);
		for (int i = 0; i < 8; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		ci_arr_set(a, 0, IPTR(100));
		ci_arr_set(a, 7, IPTR(107));
		assert(ci_arr_index(a, 0) == IPTR(100));
		assert(ci_arr_index(a, 7) == IPTR(107));
		/* neighbors unchanged */
		assert(ci_arr_index(a, 1) == IPTR(1));
		assert(ci_arr_index(a, 6) == IPTR(6));
		ci_free(a);
	}

	/* --- Set on wrapped array: element at physical wrap point --- */
	{
		ci_array *a = ci_arr_new(8);
		for (int i = 0; i < 8; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		/* shift 4: offset=4, len=4, logical[0]=4 physical slot=4 */
		for (int i = 0; i < 4; i++) {
			assert(ci_arr_shift(a) == IPTR(i));
		}
		/* push 4 more: wraps around — logical[4..7] at physical[0..3] */
		for (int i = 8; i < 12; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		/* logical: 4 5 6 7 8 9 10 11; physical wrap at index 4 */
		/* set at the wrap boundary (logical index 4 = physical 0) */
		ci_arr_set(a, 4, IPTR(200));
		assert(ci_arr_index(a, 4) == IPTR(200));
		assert(ci_arr_index(a, 3) == IPTR(7));
		assert(ci_arr_index(a, 5) == IPTR(9));
		ci_free(a);
	}

	/* --- Set after shift (offset != 0): verify physical slot is correct --- */
	{
		ci_array *a = ci_arr_new(8);
		for (int i = 0; i < 8; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		for (int i = 0; i < 3; i++) {
			assert(ci_arr_shift(a) == IPTR(i));
		}
		/* offset=3, logical[0]=IPTR(3), logical[4]=IPTR(7) */
		assert(a->offset == 3);
		ci_arr_set(a, 0, IPTR(300));
		ci_arr_set(a, 4, IPTR(304));
		assert(ci_arr_index(a, 0) == IPTR(300));
		assert(ci_arr_index(a, 4) == IPTR(304));
		assert(ci_arr_index(a, 1) == IPTR(4));
		assert(ci_arr_index(a, 3) == IPTR(6));
		ci_free(a);
	}

	/* --- Read-modify-write: index → modify → set → verify --- */
	{
		ci_array *a = ci_arr_new(8);
		for (int i = 0; i < 8; i++) {
			assert(ci_arr_push(a, IPTR(i * 10)));
		}
		/* "modify" = pick a new sentinel: read index 5, replace with IPTR(99) */
		ci_ptr old = ci_arr_index(a, 5);
		assert(old == IPTR(50));
		ci_arr_set(a, 5, IPTR(99));
		assert(ci_arr_index(a, 5) == IPTR(99));
		/* surrounding elements unchanged */
		assert(ci_arr_index(a, 4) == IPTR(40));
		assert(ci_arr_index(a, 6) == IPTR(60));
		ci_free(a);
	}

	/* --- After ensure_space/realloc: set still works on correct logical index --- */
	{
		ci_array *a = ci_arr_new(4);
		for (int i = 0; i < 4; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		/* shift 2, push 2 to wrap, push 1 more to trigger realloc */
		for (int i = 0; i < 2; i++) {
			assert(ci_arr_shift(a) == IPTR(i));
		}
		for (int i = 4; i < 6; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		/* now full (len=4, size=4, wrapped) */
		assert(ci_arr_push(a, IPTR(6))); /* triggers realloc + linearize */
		assert(a->offset == 0);
		/* logical: 2 3 4 5 6 */
		assert(ci_arr_len(a) == 5);
		ci_arr_set(a, 0, IPTR(400));
		ci_arr_set(a, 4, IPTR(404));
		assert(ci_arr_index(a, 0) == IPTR(400));
		assert(ci_arr_index(a, 4) == IPTR(404));
		assert(ci_arr_index(a, 2) == IPTR(4));
		ci_free(a);
	}

	teardown();
	printf("test_set_index: PASSED\n");
	return 0;
}
