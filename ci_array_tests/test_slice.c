/* test_slice.c — ci_arr_slice and ci_arr_copy: all documented cases */
#include "ciobj.c"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>

static void setup(void)    { ci_init(); ci_str_register(); ci_arr_register(); }
static void teardown(void) { ci_shutdown(); }

static int dummy[32];
#define PTR(i) ((ci_ptr)&dummy[i])

/* build array [PTR(0), PTR(1), ..., PTR(n-1)] */
static ci_array *make(int n) {
	ci_array *a = ci_arr_new((uint32_t)n);
	for (int i = 0; i < n; i++) {
		ci_arr_push(a, PTR(i));
	}
	return a;
}

/* assert slice result matches expected PTR indices */
static void check(ci_array *s, int *expect, int len) {
	assert((int)ci_arr_len(s) == len);
	for (int i = 0; i < len; i++) {
		assert(ci_arr_index(s, (uint32_t)i) == PTR(expect[i]));
	}
}

int main(void) {
	setup();

	/* --- full copy: slice(a, 0, INT32_MAX) --- */
	{
		ci_array *a = make(5); /* [0,1,2,3,4] */
		ci_array *s = ci_arr_slice(a, 0, INT32_MAX);
		assert(s != NULL);
		int exp[] = {0,1,2,3,4};
		check(s, exp, 5);
		ci_free(s); ci_free(a);
	}
	{
		ci_array *a = make(3); /* [0,1,2] */
		ci_array *s = ci_arr_slice(a, 0, INT32_MAX);
		assert(s != NULL);
		int exp[] = {0,1,2};
		check(s, exp, 3);
		ci_free(s); ci_free(a);
	}

	/* --- from index n to end: slice(a, n, INT32_MAX) --- */
	{
		ci_array *a = make(6); /* [0,1,2,3,4,5] */
		ci_array *s = ci_arr_slice(a, 2, INT32_MAX);
		int exp[] = {2,3,4,5};
		check(s, exp, 4);
		ci_free(s); ci_free(a);
	}
	{
		ci_array *a = make(5); /* [0,1,2,3,4] */
		ci_array *s = ci_arr_slice(a, 4, INT32_MAX); /* last one */
		int exp[] = {4};
		check(s, exp, 1);
		ci_free(s); ci_free(a);
	}

	/* --- first n elements: slice(a, 0, n) --- */
	{
		ci_array *a = make(6);
		ci_array *s = ci_arr_slice(a, 0, 3);
		int exp[] = {0,1,2};
		check(s, exp, 3);
		ci_free(s); ci_free(a);
	}
	{
		ci_array *a = make(4);
		ci_array *s = ci_arr_slice(a, 0, 2);
		int exp[] = {0,1};
		check(s, exp, 2);
		ci_free(s); ci_free(a);
	}

	/* --- last n elements: slice(a, -n, INT32_MAX) --- */
	{
		ci_array *a = make(6);
		ci_array *s = ci_arr_slice(a, -3, INT32_MAX);
		int exp[] = {3,4,5};
		check(s, exp, 3);
		ci_free(s); ci_free(a);
	}
	{
		ci_array *a = make(5);
		ci_array *s = ci_arr_slice(a, -2, INT32_MAX);
		int exp[] = {3,4};
		check(s, exp, 2);
		ci_free(s); ci_free(a);
	}

	/* --- all except last n: slice(a, 0, -n) --- */
	{
		ci_array *a = make(5);
		ci_array *s = ci_arr_slice(a, 0, -1);
		int exp[] = {0,1,2,3};
		check(s, exp, 4);
		ci_free(s); ci_free(a);
	}
	{
		ci_array *a = make(6);
		ci_array *s = ci_arr_slice(a, 0, -2);
		int exp[] = {0,1,2,3};
		check(s, exp, 4);
		ci_free(s); ci_free(a);
	}

	/* --- both negative: slice(a, -m, -n) --- */
	{
		ci_array *a = make(8); /* [0..7] */
		ci_array *s = ci_arr_slice(a, -5, -2); /* indices 3,4,5 */
		int exp[] = {3,4,5};
		check(s, exp, 3);
		ci_free(s); ci_free(a);
	}
	{
		ci_array *a = make(6); /* [0..5] */
		ci_array *s = ci_arr_slice(a, -4, -1); /* indices 2,3,4 */
		int exp[] = {2,3,4};
		check(s, exp, 3);
		ci_free(s); ci_free(a);
	}

	/* --- explicit mid-range: slice(a, 2, 5) --- */
	{
		ci_array *a = make(8);
		ci_array *s = ci_arr_slice(a, 2, 5); /* elements 2,3,4 */
		int exp[] = {2,3,4};
		check(s, exp, 3);
		ci_free(s); ci_free(a);
	}
	{
		ci_array *a = make(10);
		ci_array *s = ci_arr_slice(a, 3, 7); /* elements 3,4,5,6 */
		int exp[] = {3,4,5,6};
		check(s, exp, 4);
		ci_free(s); ci_free(a);
	}

	/* --- single element: slice(a, n, n+1) --- */
	{
		ci_array *a = make(5);
		ci_array *s = ci_arr_slice(a, 2, 3);
		int exp[] = {2};
		check(s, exp, 1);
		ci_free(s); ci_free(a);
	}
	{
		ci_array *a = make(5);
		ci_array *s = ci_arr_slice(a, 0, 1);
		int exp[] = {0};
		check(s, exp, 1);
		ci_free(s); ci_free(a);
	}

	/* --- empty: zero-length range slice(a, n, n) --- */
	{
		ci_array *a = make(5);
		ci_array *s = ci_arr_slice(a, 2, 2);
		assert(ci_arr_len(s) == 0);
		ci_free(s); ci_free(a);
	}
	{
		ci_array *a = make(5);
		ci_array *s = ci_arr_slice(a, 0, 0);
		assert(ci_arr_len(s) == 0);
		ci_free(s); ci_free(a);
	}

	/* --- inverted range: slice(a, 5, 2) → empty --- */
	{
		ci_array *a = make(8);
		ci_array *s = ci_arr_slice(a, 5, 2);
		assert(s != NULL);
		assert(ci_arr_len(s) == 0);
		ci_free(s); ci_free(a);
	}
	{
		ci_array *a = make(6);
		ci_array *s = ci_arr_slice(a, 4, 1);
		assert(s != NULL);
		assert(ci_arr_len(s) == 0);
		ci_free(s); ci_free(a);
	}

	/* --- out-of-bounds clamping: slice(a, 0, 9999) → entire array --- */
	{
		ci_array *a = make(4);
		ci_array *s = ci_arr_slice(a, 0, 9999);
		int exp[] = {0,1,2,3};
		check(s, exp, 4);
		ci_free(s); ci_free(a);
	}
	{
		ci_array *a = make(3);
		ci_array *s = ci_arr_slice(a, 9999, INT32_MAX); /* from beyond end */
		assert(ci_arr_len(s) == 0);
		ci_free(s); ci_free(a);
	}

	/* --- negative clamping: from way before start --- */
	{
		ci_array *a = make(4);
		ci_array *s = ci_arr_slice(a, -999, 3); /* clamps from to 0 */
		int exp[] = {0,1,2};
		check(s, exp, 3);
		ci_free(s); ci_free(a);
	}
	{
		ci_array *a = make(4);
		ci_array *s = ci_arr_slice(a, -999, INT32_MAX); /* clamps from to 0 → full copy */
		int exp[] = {0,1,2,3};
		check(s, exp, 4);
		ci_free(s); ci_free(a);
	}

	/* --- slice on empty array --- */
	{
		ci_array *a = ci_arr_new(4);
		ci_array *s = ci_arr_slice(a, 0, INT32_MAX);
		assert(s != NULL);
		assert(ci_arr_len(s) == 0);
		ci_free(s); ci_free(a);
	}
	{
		ci_array *a = ci_arr_new(4);
		ci_array *s = ci_arr_slice(a, -1, INT32_MAX);
		assert(s != NULL);
		assert(ci_arr_len(s) == 0);
		ci_free(s); ci_free(a);
	}

	/* --- slice on circular-buffer source (actual wrap in backing store) --- */
	{
		/* size=4, push 0,1,2,3 → offset=0, data=[0,1,2,3]
		 * shift → offset=1, logical=[1,2,3]
		 * push 4 → data[(1+3)%4]=data[0]=4, logical=[1,2,3,4]
		 * data=[4,1,2,3], offset=1 — element 4 wraps to slot 0 */
		ci_array *a = ci_arr_new(4);
		ci_arr_push(a, PTR(0)); ci_arr_push(a, PTR(1));
		ci_arr_push(a, PTR(2)); ci_arr_push(a, PTR(3));
		ci_arr_shift(a);
		ci_arr_push(a, PTR(4));
		assert(a->offset == 1); /* confirm wrap */
		assert(ci_arr_len(a) == 4);

		ci_array *s = ci_arr_slice(a, 1, 3); /* logical 1,2 → PTR(2),PTR(3) */
		int exp[] = {2,3};
		check(s, exp, 2);
		ci_free(s); ci_free(a);
	}
	{
		/* size=4, push 0,1,2,3 → offset=0
		 * shift twice → offset=2, logical=[2,3]
		 * push 4,5 → data[0]=4, data[1]=5, logical=[2,3,4,5]
		 * data=[4,5,2,3], offset=2 — tail wraps around */
		ci_array *a = ci_arr_new(4);
		ci_arr_push(a, PTR(0)); ci_arr_push(a, PTR(1));
		ci_arr_push(a, PTR(2)); ci_arr_push(a, PTR(3));
		ci_arr_shift(a); ci_arr_shift(a);
		ci_arr_push(a, PTR(4)); ci_arr_push(a, PTR(5));
		assert(a->offset == 2); /* confirm wrap */
		assert(ci_arr_len(a) == 4);

		ci_array *s = ci_arr_slice(a, -2, INT32_MAX); /* last 2 → PTR(4),PTR(5) */
		int exp[] = {4,5};
		check(s, exp, 2);
		ci_free(s); ci_free(a);
	}

	/* --- ci_arr_copy: identical to slice(0, INT32_MAX) --- */
	{
		ci_array *a = make(5);
		ci_array *c = ci_arr_copy(a);
		assert(c != a); /* distinct object */
		int exp[] = {0,1,2,3,4};
		check(c, exp, 5);
		ci_free(c); ci_free(a);
	}
	{
		ci_array *a = make(1);
		ci_array *c = ci_arr_copy(a);
		assert(c != a);
		int exp[] = {0};
		check(c, exp, 1);
		ci_free(c); ci_free(a);
	}

	/* --- copy of empty array --- */
	{
		ci_array *a = ci_arr_new(4);
		ci_array *c = ci_arr_copy(a);
		assert(c != NULL);
		assert(ci_arr_len(c) == 0);
		ci_free(c); ci_free(a);
	}

	teardown();
	printf("test_slice: PASSED\n");
	return 0;
}
