/* test_push_pop_stress.c — large-scale push/pop */
#include "ciobj.c"
#include <assert.h>
#include <stdio.h>

static void setup(void)    { ci_init(); ci_str_register(); ci_arr_register(); }
static void teardown(void) { ci_shutdown(); }

#define IPTR(i) ((ci_ptr)(uintptr_t)((uintptr_t)(i) * 8 + 8))
#define N 10000

int main(void) {
	setup();

	/* --- Push 10000 elements, verify all via index, pop all in reverse --- */
	{
		ci_array *a = ci_arr_new(16);
		for (int i = 0; i < N; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		assert(ci_arr_len(a) == N);

		for (int i = 0; i < N; i++) {
			assert(ci_arr_index(a, (uint32_t)i) == IPTR(i));
		}
		for (int i = N - 1; i >= 0; i--) {
			assert(ci_arr_pop(a) == IPTR(i));
		}
		assert(ci_arr_len(a) == 0);
		assert(ci_arr_pop(a) == NULL);
		ci_free(a);
	}

	/* --- Push 10000, shift all in forward order --- */
	{
		ci_array *a = ci_arr_new(16);
		for (int i = 0; i < N; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		for (int i = 0; i < N; i++) {
			assert(ci_arr_shift(a) == IPTR(i));
		}
		assert(ci_arr_len(a) == 0);
		assert(ci_arr_shift(a) == NULL);
		ci_free(a);
	}

	/* --- Alternate: push 3, pop 1, repeat until 5000 net elements, verify all ---
	 * Each batch: push IPTR(base), IPTR(base+1), IPTR(base+2), pop (removes base+2).
	 * Surviving elements: IPTR(3k) and IPTR(3k+1) for k=0..2499.
	 * logical index i: IPTR(3*(i/2) + (i%2)) */
	{
		ci_array *a = ci_arr_new(16);
		int base = 0;
		int net = 0;

		while (net < 5000) {
			assert(ci_arr_push(a, IPTR(base)));
			assert(ci_arr_push(a, IPTR(base + 1)));
			assert(ci_arr_push(a, IPTR(base + 2)));
			ci_ptr popped = ci_arr_pop(a);
			assert(popped == IPTR(base + 2));
			base += 3;
			net += 2;
		}
		assert(ci_arr_len(a) == 5000);

		for (int i = 0; i < 5000; i++) {
			int expected = 3 * (i / 2) + (i % 2);
			assert(ci_arr_index(a, (uint32_t)i) == IPTR(expected));
		}
		ci_free(a);
	}

	/* --- Push 1000, pop 500, push 1000, pop 500 — verify remaining 1000 --- */
	{
		ci_array *a = ci_arr_new(16);

		for (int i = 0; i < 1000; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		/* pop 500 from tail: removes IPTR(999)..IPTR(500) */
		for (int i = 999; i >= 500; i--) {
			assert(ci_arr_pop(a) == IPTR(i));
		}
		assert(ci_arr_len(a) == 500);

		for (int i = 1000; i < 2000; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		/* pop 500 from tail: removes IPTR(1999)..IPTR(1500) */
		for (int i = 1999; i >= 1500; i--) {
			assert(ci_arr_pop(a) == IPTR(i));
		}
		assert(ci_arr_len(a) == 1000);

		/* remaining: IPTR(0)..IPTR(499), IPTR(1000)..IPTR(1499) */
		for (int i = 0; i < 500; i++) {
			assert(ci_arr_index(a, (uint32_t)i) == IPTR(i));
		}
		for (int i = 0; i < 500; i++) {
			assert(ci_arr_index(a, (uint32_t)(500 + i)) == IPTR(1000 + i));
		}
		ci_free(a);
	}

	teardown();
	printf("test_push_pop_stress: PASSED\n");
	return 0;
}
