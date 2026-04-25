/* test_deque_pattern.c — double-ended queue usage patterns */
#include "ciobj.c"
#include <assert.h>
#include <stdio.h>

static void setup(void)    { ci_init(); ci_str_register(); ci_arr_register(); }
static void teardown(void) { ci_shutdown(); }

#define IPTR(i) ((ci_ptr)(uintptr_t)((uintptr_t)(i) * 8 + 8))

int main(void) {
	setup();

	/* --- Scenario 1: FIFO via push+shift ---
	 * push 0..999, shift all in insertion order */
	{
		ci_array *a = ci_arr_new(16);
		for (int i = 0; i < 1000; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		for (int i = 0; i < 1000; i++) {
			assert(ci_arr_shift(a) == IPTR(i));
		}
		assert(ci_arr_len(a) == 0);
		ci_free(a);
	}

	/* --- Scenario 2: FIFO via unshift+pop ---
	 * unshift 0..999, pop all in insertion order.
	 * unshift order 0,1,...,999 → logical [999,998,...,0].
	 * pop removes tail: IPTR(0), IPTR(1), ..., IPTR(999). */
	{
		ci_array *a = ci_arr_new(16);
		for (int i = 0; i < 1000; i++) {
			assert(ci_arr_unshift(a, IPTR(i)));
		}
		for (int i = 0; i < 1000; i++) {
			assert(ci_arr_pop(a) == IPTR(i));
		}
		assert(ci_arr_len(a) == 0);
		ci_free(a);
	}

	/* --- Scenario 3: interleaved producer/consumer ---
	 * push 3 items per iteration, shift 1; consume 10000 total.
	 * Produced values are sequential: IPTR(0), IPTR(1), ...
	 * shift always yields the oldest: sequence 0, 1, 2, ... */
	{
		ci_array *a = ci_arr_new(16);
		int produced = 0;
		int consumed = 0;

		while (consumed < 10000) {
			/* produce 3 */
			for (int i = 0; i < 3; i++) {
				assert(ci_arr_push(a, IPTR(produced++)));
			}
			/* consume 1 */
			assert(ci_arr_shift(a) == IPTR(consumed++));
		}

		/* drain remaining */
		while (ci_arr_len(a) > 0) {
			assert(ci_arr_shift(a) == IPTR(consumed++));
		}
		assert(consumed == produced);
		ci_free(a);
	}

	/* --- Scenario 4: bounded buffer — push then shift in lockstep ---
	 * Always 1 element at a time: push(i), shift() == i.
	 * Array never grows beyond 1 element; size stays at 64. */
	{
		ci_array *a = ci_arr_new(64);
		assert(ci_arr_size(a) == 64);

		for (int i = 0; i < 10000; i++) {
			assert(ci_arr_push(a, IPTR(i)));
			assert(ci_arr_shift(a) == IPTR(i));
		}
		assert(ci_arr_len(a) == 0);
		assert(ci_arr_size(a) == 64); /* no growth */
		ci_free(a);
	}

	teardown();
	printf("test_deque_pattern: PASSED\n");
	return 0;
}
