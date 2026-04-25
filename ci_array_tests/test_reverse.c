/* test_reverse.c — ci_arr_reverse: empty, single, even, odd, circular offset */
#include "ciobj.c"
#include <assert.h>
#include <stdio.h>

static void setup(void)    { ci_init(); ci_str_register(); ci_arr_register(); }
static void teardown(void) { ci_shutdown(); }

static int dummy[32];
#define PTR(i) ((ci_ptr)&dummy[i])

int main(void) {
	setup();

	/* --- reverse empty array: must not crash --- */
	{
		ci_array *a = ci_arr_new(8);
		assert(ci_arr_len(a) == 0);
		ci_arr_reverse(a); /* bug: hi = 0-1 = UINT32_MAX, loop runs */
		assert(ci_arr_len(a) == 0);
		ci_free(a);
	}

	/* --- reverse single element: no-op --- */
	{
		ci_array *a = ci_arr_new(8);
		ci_arr_push(a, PTR(7));
		ci_arr_reverse(a);
		assert(ci_arr_len(a) == 1);
		assert(ci_arr_index(a, 0) == PTR(7));
		ci_free(a);
	}

	/* --- reverse two elements --- */
	{
		ci_array *a = ci_arr_new(8);
		ci_arr_push(a, PTR(0));
		ci_arr_push(a, PTR(1));
		ci_arr_reverse(a);
		assert(ci_arr_index(a, 0) == PTR(1));
		assert(ci_arr_index(a, 1) == PTR(0));
		ci_free(a);
	}

	/* --- reverse odd-length array --- */
	{
		ci_array *a = ci_arr_new(8);
		for (int i = 0; i < 5; i++) ci_arr_push(a, PTR(i));
		ci_arr_reverse(a);
		assert(ci_arr_index(a, 0) == PTR(4));
		assert(ci_arr_index(a, 1) == PTR(3));
		assert(ci_arr_index(a, 2) == PTR(2));
		assert(ci_arr_index(a, 3) == PTR(1));
		assert(ci_arr_index(a, 4) == PTR(0));
		ci_free(a);
	}

	/* --- reverse even-length array --- */
	{
		ci_array *a = ci_arr_new(8);
		for (int i = 0; i < 6; i++) ci_arr_push(a, PTR(i));
		ci_arr_reverse(a);
		assert(ci_arr_index(a, 0) == PTR(5));
		assert(ci_arr_index(a, 5) == PTR(0));
		ci_free(a);
	}

	/* --- reverse with non-zero circular offset --- */
	{
		/* create offset by unshifting: logical [3,2,1,0] with wrapped offset */
		ci_array *a = ci_arr_new(8);
		ci_arr_push(a, PTR(0));
		ci_arr_push(a, PTR(1));
		ci_arr_push(a, PTR(2));
		ci_arr_unshift(a, PTR(3)); /* offset now at size-1 */
		/* logical order: 3 0 1 2 */
		ci_arr_reverse(a);
		/* expected: 2 1 0 3 */
		assert(ci_arr_len(a) == 4);
		assert(ci_arr_index(a, 0) == PTR(2));
		assert(ci_arr_index(a, 1) == PTR(1));
		assert(ci_arr_index(a, 2) == PTR(0));
		assert(ci_arr_index(a, 3) == PTR(3));
		ci_free(a);
	}

	teardown();
	printf("test_reverse: PASSED\n");
	return 0;
}
