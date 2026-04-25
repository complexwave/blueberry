/* test_shift_unshift_stress.c — large-scale head operations */
#include "ciobj.c"
#include <assert.h>
#include <stdio.h>

static void setup(void)    { ci_init(); ci_str_register(); ci_arr_register(); }
static void teardown(void) { ci_shutdown(); }

#define IPTR(i) ((ci_ptr)(uintptr_t)((uintptr_t)(i) * 8 + 8))
#define N 10000

int main(void) {
	setup();

	/* --- Unshift 10000 elements, verify all via index, shift all ---
	 * Each unshift prepends, so logical[0] = most recently unshifted.
	 * After unshift 0..9999: logical[i] = IPTR(9999 - i). */
	{
		ci_array *a = ci_arr_new(16);
		for (int i = 0; i < N; i++) {
			assert(ci_arr_unshift(a, IPTR(i)));
		}
		assert(ci_arr_len(a) == N);

		for (int i = 0; i < N; i++) {
			assert(ci_arr_index(a, (uint32_t)i) == IPTR(N - 1 - i));
		}
		/* shift all: FIFO from logical head → IPTR(9999), IPTR(9998), ..., IPTR(0) */
		for (int i = N - 1; i >= 0; i--) {
			assert(ci_arr_shift(a) == IPTR(i));
		}
		assert(ci_arr_len(a) == 0);
		assert(ci_arr_shift(a) == NULL);
		ci_free(a);
	}

	/* --- Unshift 1000, pop 500, unshift 500 — verify order ---
	 * After unshift 0..999: logical = [999, 998, ..., 1, 0].
	 * Pop 500: removes tail (IPTR(0)..IPTR(499)). Remaining: [999, 998, ..., 500].
	 * Unshift 1000..1499: each goes before IPTR(999).
	 * Final: [1499, 1498, ..., 1000, 999, 998, ..., 500].
	 * Index i: IPTR(1499 - i) for i = 0..999. */
	{
		ci_array *a = ci_arr_new(16);
		for (int i = 0; i < 1000; i++) {
			assert(ci_arr_unshift(a, IPTR(i)));
		}
		for (int i = 0; i < 500; i++) {
			assert(ci_arr_pop(a) == IPTR(i));
		}
		assert(ci_arr_len(a) == 500);

		for (int i = 1000; i < 1500; i++) {
			assert(ci_arr_unshift(a, IPTR(i)));
		}
		assert(ci_arr_len(a) == 1000);

		for (int i = 0; i < 1000; i++) {
			assert(ci_arr_index(a, (uint32_t)i) == IPTR(1499 - i));
		}
		ci_free(a);
	}

	/* --- Deque pattern: unshift+pop as FIFO queue ---
	 * Unshift 0..999 (prepend each), then pop all.
	 * Unshift order: 0, 1, 2, ..., 999 → logical: [999, 998, ..., 0].
	 * Pop removes tail: IPTR(0), IPTR(1), ..., IPTR(999). (FIFO of unshift sequence) */
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

	/* --- Mixed: interleaved unshift and shift cycling ---
	 * Start with 8 elements, repeat 100 times: unshift new, shift old from tail.
	 * This is unshift+pop but cycling — unshift new at head, pop old at tail. */
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
			/* shift from head removes window[0] */
			assert(ci_arr_shift(a) == IPTR(window[0]));
			/* unshift new at head — goes before remaining window[1..7] */
			assert(ci_arr_unshift(a, IPTR(next_val)));
			/* new logical: [next_val, window[1], ..., window[7]] */
			window[0] = next_val;
			next_val++;
			for (int i = 0; i < 8; i++) {
				assert(ci_arr_index(a, (uint32_t)i) == IPTR(window[i]));
			}
		}
		ci_free(a);
	}

	teardown();
	printf("test_shift_unshift_stress: PASSED\n");
	return 0;
}
