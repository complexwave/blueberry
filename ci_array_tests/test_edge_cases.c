/* test_edge_cases.c — boundary conditions */
#include "ciobj.c"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>

static void setup(void)    { ci_init(); ci_str_register(); ci_arr_register(); }
static void teardown(void) { ci_shutdown(); }

#define IPTR(i) ((ci_ptr)(uintptr_t)((uintptr_t)(i) * 8 + 8))

int main(void) {
	setup();

	/* --- Array of size 1: all 4 operation pairs --- */
	{
		/* push/pop */
		ci_array *a = ci_arr_new(1);
		assert(ci_arr_push(a, IPTR(1)));
		assert(ci_arr_pop(a) == IPTR(1));
		assert(ci_arr_len(a) == 0);
		ci_free(a);
	}
	{
		/* push/shift */
		ci_array *a = ci_arr_new(1);
		assert(ci_arr_push(a, IPTR(2)));
		assert(ci_arr_shift(a) == IPTR(2));
		assert(ci_arr_len(a) == 0);
		ci_free(a);
	}
	{
		/* unshift/pop */
		ci_array *a = ci_arr_new(1);
		assert(ci_arr_unshift(a, IPTR(3)));
		assert(ci_arr_pop(a) == IPTR(3));
		assert(ci_arr_len(a) == 0);
		ci_free(a);
	}
	{
		/* unshift/shift */
		ci_array *a = ci_arr_new(1);
		assert(ci_arr_unshift(a, IPTR(4)));
		assert(ci_arr_shift(a) == IPTR(4));
		assert(ci_arr_len(a) == 0);
		ci_free(a);
	}

	/* --- Array of size 2: all 4 operation pairs --- */
	{
		ci_array *a = ci_arr_new(2);
		assert(ci_arr_push(a, IPTR(10)));
		assert(ci_arr_push(a, IPTR(11)));
		assert(ci_arr_pop(a) == IPTR(11));
		assert(ci_arr_pop(a) == IPTR(10));
		assert(ci_arr_len(a) == 0);
		ci_free(a);
	}
	{
		ci_array *a = ci_arr_new(2);
		assert(ci_arr_push(a, IPTR(10)));
		assert(ci_arr_push(a, IPTR(11)));
		assert(ci_arr_shift(a) == IPTR(10));
		assert(ci_arr_shift(a) == IPTR(11));
		assert(ci_arr_len(a) == 0);
		ci_free(a);
	}
	{
		ci_array *a = ci_arr_new(2);
		assert(ci_arr_unshift(a, IPTR(10)));
		assert(ci_arr_unshift(a, IPTR(11)));
		assert(ci_arr_pop(a) == IPTR(10));
		assert(ci_arr_pop(a) == IPTR(11));
		assert(ci_arr_len(a) == 0);
		ci_free(a);
	}
	{
		ci_array *a = ci_arr_new(2);
		assert(ci_arr_unshift(a, IPTR(10)));
		assert(ci_arr_unshift(a, IPTR(11)));
		assert(ci_arr_shift(a) == IPTR(11));
		assert(ci_arr_shift(a) == IPTR(10));
		assert(ci_arr_len(a) == 0);
		ci_free(a);
	}

	/* --- Push to exactly full (no realloc), then push one more (triggers realloc) --- */
	{
		ci_array *a = ci_arr_new(8);
		uint32_t cap = ci_arr_size(a);
		for (uint32_t i = 0; i < cap; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		assert(ci_arr_len(a) == cap);
		assert(ci_arr_size(a) == cap); /* exactly full, no realloc yet */

		assert(ci_arr_push(a, IPTR(cap))); /* triggers realloc */
		assert(ci_arr_len(a) == cap + 1);
		assert(ci_arr_size(a) > cap);
		for (uint32_t i = 0; i <= cap; i++) {
			assert(ci_arr_index(a, i) == IPTR(i));
		}
		ci_free(a);
	}

	/* --- ensure_space(0) when full → no realloc --- */
	{
		ci_array *a = ci_arr_new(8);
		uint32_t cap = ci_arr_size(a);
		for (uint32_t i = 0; i < cap; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		assert(ci_arr_len(a) == cap);
		assert(ci_arr_size(a) == cap);
		assert(ci_arr_ensure_space(a, 0));
		assert(ci_arr_size(a) == cap); /* no realloc */
		ci_free(a);
	}

	/* --- ensure_space(UINT32_MAX) → OOM, returns 0 gracefully, no crash --- */
	{
		ci_array *a = ci_arr_new(4);
		int ok = ci_arr_ensure_space(a, UINT32_MAX);
		(void)ok; /* may succeed or fail, must not crash */
		/* if it failed, a must still be valid and functional */
		if (!ok) {
			assert(ci_arr_push(a, IPTR(1)));
			assert(ci_arr_pop(a) == IPTR(1));
		}
		ci_free(a);
	}

	/* --- Pop/shift return NULL on empty (not crash) --- */
	{
		ci_array *a = ci_arr_new(4);
		assert(ci_arr_pop(a) == NULL);
		assert(ci_arr_shift(a) == NULL);
		assert(ci_arr_len(a) == 0);

		ci_arr_push(a, IPTR(1));
		assert(ci_arr_pop(a) == IPTR(1));
		assert(ci_arr_pop(a) == NULL);
		assert(ci_arr_shift(a) == NULL);
		ci_free(a);
	}

	teardown();
	printf("test_edge_cases: PASSED\n");
	return 0;
}
