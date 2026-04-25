/* test_mixed_operations.c — combined workflows */
#include "ciobj.c"
#include <assert.h>
#include <stdio.h>

static void setup(void)    { ci_init(); ci_str_register(); ci_arr_register(); }
static void teardown(void) { ci_shutdown(); }

#define IPTR(i) ((ci_ptr)(uintptr_t)((uintptr_t)(i) * 8 + 8))

int main(void) {
	setup();

	/* --- new → push 10 → shift 5 → push 5 → index all → pop all → verify order ---
	 * After push 0..9, shift 5 (removes 0..4), push 10..14:
	 * logical: 5 6 7 8 9 10 11 12 13 14
	 * pop all in reverse: 14 13 12 11 10 9 8 7 6 5 */
	{
		ci_array *a = ci_arr_new(16);
		for (int i = 0; i < 10; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		for (int i = 0; i < 5; i++) {
			assert(ci_arr_shift(a) == IPTR(i));
		}
		for (int i = 10; i < 15; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		assert(ci_arr_len(a) == 10);
		for (int i = 0; i < 10; i++) {
			assert(ci_arr_index(a, (uint32_t)i) == IPTR(5 + i));
		}
		for (int i = 9; i >= 0; i--) {
			assert(ci_arr_pop(a) == IPTR(5 + i));
		}
		assert(ci_arr_len(a) == 0);
		ci_free(a);
	}

	/* --- new_inline → fill → upgrade → push more → shift half → verify --- */
	{
		ci_array *a = ci_arr_new_inline(1);
		uint32_t cap = ci_arr_size(a);
		assert(CI_IS_ARR_SMALL(a));

		/* fill to capacity */
		for (uint32_t i = 0; i < cap; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		/* push one more → upgrade */
		assert(ci_arr_push(a, IPTR(cap)));
		assert(!CI_IS_ARR_SMALL(a));

		/* push 10 more */
		for (uint32_t i = 1; i <= 10; i++) {
			assert(ci_arr_push(a, IPTR(cap + i)));
		}
		/* total len = cap + 11 */
		assert(ci_arr_len(a) == cap + 11);

		/* shift half of total */
		uint32_t half = (cap + 11) / 2;
		for (uint32_t i = 0; i < half; i++) {
			assert(ci_arr_shift(a) == IPTR(i));
		}
		assert(ci_arr_len(a) == cap + 11 - half);

		/* verify remaining */
		for (uint32_t i = 0; i < cap + 11 - half; i++) {
			assert(ci_arr_index(a, i) == IPTR(half + i));
		}
		ci_free(a);
	}

	/* --- new → push → clear → push different → verify new data --- */
	{
		ci_array *a = ci_arr_new(8);
		for (int i = 0; i < 8; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		ci_arr_clear(a);
		assert(ci_arr_len(a) == 0);

		for (int i = 100; i < 108; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		assert(ci_arr_len(a) == 8);
		for (int i = 0; i < 8; i++) {
			assert(ci_arr_index(a, (uint32_t)i) == IPTR(100 + i));
		}
		ci_free(a);
	}

	/* --- Create array, fill, ensure_space, push more, pop all → verify full sequence --- */
	{
		ci_array *a = ci_arr_new(8);
		for (int i = 0; i < 8; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		assert(ci_arr_ensure_space(a, 100));
		assert(ci_arr_size(a) >= 108);

		for (int i = 8; i < 108; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		assert(ci_arr_len(a) == 108);

		for (int i = 107; i >= 0; i--) {
			assert(ci_arr_pop(a) == IPTR(i));
		}
		assert(ci_arr_len(a) == 0);
		ci_free(a);
	}

	/* --- Nested: array of pointer values (not real refs) — push arrays as ptrs --- */
	{
		ci_array *outer = ci_arr_new(4);
		ci_array *inner[4];

		for (int i = 0; i < 4; i++) {
			inner[i] = ci_arr_new(4);
			assert(inner[i] != NULL);
			/* store the inner array pointer as a value (not a real ref) */
			assert(ci_arr_push(outer, (ci_ptr)inner[i]));
		}
		assert(ci_arr_len(outer) == 4);
		for (int i = 0; i < 4; i++) {
			assert(ci_arr_index(outer, (uint32_t)i) == (ci_ptr)inner[i]);
		}
		/* free inner arrays explicitly, then outer */
		for (int i = 0; i < 4; i++) {
			ci_free(inner[i]);
		}
		ci_free(outer);
	}

	teardown();
	printf("test_mixed_operations: PASSED\n");
	return 0;
}
