/* test_destructor.c — lifecycle and cleanup */
#include "ciobj.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static void setup(void)    { ci_init(); ci_str_register(); ci_arr_register(); }
static void teardown(void) { ci_shutdown(); }

#define IPTR(i) ((ci_ptr)(uintptr_t)((uintptr_t)(i) * 8 + 8))

int main(void) {
	setup();

	/* --- ci_free on full array: no crash --- */
	{
		ci_array *a = ci_arr_new(8);
		for (int i = 0; i < 8; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		ci_free(a);
	}

	/* --- ci_free on inline array: no crash --- */
	{
		ci_array *a = ci_arr_new_inline(4);
		assert(CI_IS_ARR_SMALL(a));
		ci_arr_push(a, IPTR(1));
		ci_arr_push(a, IPTR(2));
		ci_free(a);
	}

	/* --- ci_free on upgraded-from-inline: no crash --- */
	{
		ci_array *a = ci_arr_new_inline(1);
		uint32_t cap = ci_arr_size(a);
		for (uint32_t i = 0; i <= cap; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		assert(!CI_IS_ARR_SMALL(a));
		ci_free(a);
	}

	/* --- ci_dec to 0: triggers destructor on full array --- */
	{
		ci_array *a = ci_arr_new(4);
		assert(ci_refcnt(a) == 1);
		ci_inc(a);
		assert(ci_refcnt(a) == 2);
		assert(ci_dec(a) == 0); /* refcnt → 1, not freed */
		assert(ci_dec(a) == 1); /* refcnt → 0, freed */
	}

	/* --- Allocate 100 arrays, free in reverse order: no crash --- */
	{
		ci_array *arrays[100];
		for (int i = 0; i < 100; i++) {
			arrays[i] = ci_arr_new(4);
			assert(arrays[i] != NULL);
			ci_arr_push(arrays[i], IPTR(i));
		}
		for (int i = 99; i >= 0; i--) {
			ci_free(arrays[i]);
		}
	}

	/* --- Allocate 100 arrays with elements, free in forward order: no crash --- */
	{
		ci_array *arrays[100];
		for (int i = 0; i < 100; i++) {
			arrays[i] = ci_arr_new(8);
			assert(arrays[i] != NULL);
			for (int j = 0; j < 8; j++) {
				assert(ci_arr_push(arrays[i], IPTR(j)));
			}
		}
		for (int i = 0; i < 100; i++) {
			ci_free(arrays[i]);
		}
	}

	/* --- Array containing non-NULL pointers: destructor doesn't crash
	 *     (element refcounting not implemented; pointers are just values) --- */
	{
		ci_array *a = ci_arr_new(8);
		for (int i = 0; i < 8; i++) {
			assert(ci_arr_push(a, IPTR(i + 1))); /* all non-NULL */
		}
		assert(ci_arr_len(a) == 8);
		ci_free(a); /* must not attempt to free contained pointers */
	}

	teardown();
	printf("test_destructor: PASSED\n");
	return 0;
}
