/* test_circular_wrap.c — circular buffer wrap-around correctness */
#include "ciobj.c"
#include <assert.h>
#include <stdio.h>

static void setup(void)    { ci_init(); ci_str_register(); ci_arr_register(); }
static void teardown(void) { ci_shutdown(); }

/* integer-cast sentinel pointers: unique, non-NULL, never dereferenced */
#define IPTR(i) ((ci_ptr)(uintptr_t)((uintptr_t)(i) * 8 + 8))

int main(void) {
	setup();

	/* --- Scenario 1: shift then push — offset drifts right, data wraps --- */
	{
		ci_array *a = ci_arr_new(8);
		/* push 0..7 → physical: [0 1 2 3 4 5 6 7], offset=0 */
		for (int i = 0; i < 8; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		/* shift 4 → removes 0,1,2,3; offset=4, len=4 */
		for (int i = 0; i < 4; i++) {
			assert(ci_arr_shift(a) == IPTR(i));
		}
		assert(ci_arr_len(a) == 4);
		assert(a->offset == 4);

		/* push 8,9,10,11 → physical: [8 9 10 11 4 5 6 7], offset=4, len=8 */
		for (int i = 8; i < 12; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		assert(ci_arr_len(a) == 8);

		/* verify logical: 4 5 6 7 8 9 10 11 */
		int expected1[] = {4, 5, 6, 7, 8, 9, 10, 11};
		for (int i = 0; i < 8; i++) {
			assert(ci_arr_index(a, (uint32_t)i) == IPTR(expected1[i]));
		}

		/* pop all 8 in reverse */
		for (int i = 7; i >= 0; i--) {
			assert(ci_arr_pop(a) == IPTR(expected1[i]));
		}
		assert(ci_arr_len(a) == 0);
		ci_free(a);
	}

	/* --- Scenario 2: unshift wraps offset backward from 0 --- */
	{
		ci_array *a = ci_arr_new(8);
		/* push 0..3 → offset=0, len=4 */
		for (int i = 0; i < 4; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		/* unshift 10,11,12,13 — each prepends before current head */
		assert(ci_arr_unshift(a, IPTR(10)));
		assert(ci_arr_unshift(a, IPTR(11)));
		assert(ci_arr_unshift(a, IPTR(12)));
		assert(ci_arr_unshift(a, IPTR(13)));
		assert(ci_arr_len(a) == 8);

		/* verify logical: 13 12 11 10 0 1 2 3 */
		int expected2[] = {13, 12, 11, 10, 0, 1, 2, 3};
		for (int i = 0; i < 8; i++) {
			assert(ci_arr_index(a, (uint32_t)i) == IPTR(expected2[i]));
		}

		/* shift all 8 — FIFO order */
		for (int i = 0; i < 8; i++) {
			assert(ci_arr_shift(a) == IPTR(expected2[i]));
		}
		assert(ci_arr_len(a) == 0);
		ci_free(a);
	}

	/* --- Scenario 3: interleaved shift+push cycling offset through full wrap --- */
	{
		ci_array *a = ci_arr_new(8);
		/* fill with 0..7 */
		for (int i = 0; i < 8; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}

		/* sliding window tracking expected elements */
		int window[8];
		for (int i = 0; i < 8; i++) {
			window[i] = i;
		}
		int next_val = 8;

		for (int iter = 0; iter < 100; iter++) {
			/* shift oldest: must match front of window */
			assert(ci_arr_shift(a) == IPTR(window[0]));
			/* push new at tail */
			assert(ci_arr_push(a, IPTR(next_val)));
			/* slide window */
			for (int k = 0; k < 7; k++) {
				window[k] = window[k + 1];
			}
			window[7] = next_val;
			next_val++;
			/* verify all 8 elements */
			for (int i = 0; i < 8; i++) {
				assert(ci_arr_index(a, (uint32_t)i) == IPTR(window[i]));
			}
		}
		ci_free(a);
	}

	/* --- Scenario 4: unshift+pop cycling offset backward --- */
	{
		ci_array *a = ci_arr_new(8);
		for (int i = 0; i < 8; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}

		int window[8];
		for (int i = 0; i < 8; i++) {
			window[i] = i;
		}
		int next_val = 100;

		for (int iter = 0; iter < 100; iter++) {
			/* pop tail: must match back of window */
			assert(ci_arr_pop(a) == IPTR(window[7]));
			/* unshift new at head */
			assert(ci_arr_unshift(a, IPTR(next_val)));
			/* slide window right: new value at front */
			for (int k = 7; k > 0; k--) {
				window[k] = window[k - 1];
			}
			window[0] = next_val;
			next_val++;
			/* verify all 8 elements */
			for (int i = 0; i < 8; i++) {
				assert(ci_arr_index(a, (uint32_t)i) == IPTR(window[i]));
			}
		}
		ci_free(a);
	}

	/* --- Scenario 5: single-element array — worst case for modular arithmetic --- */
	{
		ci_array *a = ci_arr_new(1);
		ci_ptr X = IPTR(42), Y = IPTR(43), Z = IPTR(44);

		assert(ci_arr_push(a, X));
		assert(ci_arr_index(a, 0) == X);
		assert(ci_arr_pop(a) == X);
		assert(ci_arr_len(a) == 0);

		assert(ci_arr_push(a, Y));
		assert(ci_arr_index(a, 0) == Y);
		assert(ci_arr_shift(a) == Y);
		assert(ci_arr_len(a) == 0);

		assert(ci_arr_unshift(a, Z));
		assert(ci_arr_index(a, 0) == Z);
		assert(ci_arr_pop(a) == Z);
		assert(ci_arr_len(a) == 0);

		ci_free(a);
	}

	teardown();
	printf("test_circular_wrap: PASSED\n");
	return 0;
}
