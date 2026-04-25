/* test_realloc_wrap.c — reallocation preserves wrapped circular data */
#include "ciobj.c"
#include <assert.h>
#include <stdio.h>
#include <stddef.h>

static void setup(void)    { ci_init(); ci_str_register(); ci_arr_register(); }
static void teardown(void) { ci_shutdown(); }

#define IPTR(i) ((ci_ptr)(uintptr_t)((uintptr_t)(i) * 8 + 8))

int main(void) {
	setup();

	/* --- Scenario 1: realloc with wrap in the middle ---
	 * push 0..7, shift 5 → offset=5,len=3; push 8..12 → full;
	 * push 13 → triggers realloc; verify [5..13], offset==0 */
	{
		ci_array *a = ci_arr_new(8);
		for (int i = 0; i < 8; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		/* shift 5 → offset=5, len=3 */
		for (int i = 0; i < 5; i++) {
			assert(ci_arr_shift(a) == IPTR(i));
		}
		assert(ci_arr_len(a) == 3);
		assert(a->offset == 5);

		/* push 8,9,10,11,12 → len=8, physically wrapped */
		for (int i = 8; i < 13; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		assert(ci_arr_len(a) == 8);
		assert(ci_arr_size(a) == 8); /* still exactly full */

		/* push 13 → triggers realloc + linearize */
		assert(ci_arr_push(a, IPTR(13)));
		assert(ci_arr_len(a) == 9);
		assert(ci_arr_size(a) >= 9);
		assert(a->offset == 0); /* linearized */

		/* verify all 9 elements: 5 6 7 8 9 10 11 12 13 */
		for (int i = 0; i < 9; i++) {
			assert(ci_arr_index(a, (uint32_t)i) == IPTR(5 + i));
		}
		ci_free(a);
	}

	/* --- Scenario 2: realloc with offset at various positions ---
	 * For each offset: fill, shift to that offset, push back to full,
	 * then push one more to trigger realloc; verify all elements. */
	{
		int offsets[] = {1, 2, 4, 7};
		int base = 0;

		for (int oi = 0; oi < 4; oi++) {
			int off = offsets[oi];
			ci_array *a = ci_arr_new(8);

			/* push 8 elements */
			for (int i = 0; i < 8; i++) {
				assert(ci_arr_push(a, IPTR(base + i)));
			}
			/* shift `off` times → offset=off, len=8-off */
			for (int i = 0; i < off; i++) {
				assert(ci_arr_shift(a) == IPTR(base + i));
			}
			/* push `off` more to fill back up → wraps physically */
			for (int i = 0; i < off; i++) {
				assert(ci_arr_push(a, IPTR(base + 8 + i)));
			}
			assert(ci_arr_len(a) == 8);
			assert(ci_arr_size(a) == 8);

			/* push one more → realloc */
			assert(ci_arr_push(a, IPTR(base + 8 + off)));
			assert(ci_arr_len(a) == 9);
			assert(ci_arr_size(a) >= 9);

			/* verify all 9: logical [base+off .. base+off+8] */
			for (int i = 0; i < 9; i++) {
				assert(ci_arr_index(a, (uint32_t)i) == IPTR(base + off + i));
			}
			ci_free(a);
			base += 20;
		}
	}

	/* --- Scenario 3: inline array wrap then upgrade ---
	 * fill to capacity, shift half, push half (create wrap),
	 * push beyond capacity → upgrade + linearize */
	{
		ci_array *a = ci_arr_new_inline(1);
		uint32_t cap = ci_arr_size(a);
		assert(cap >= 2);
		uint32_t half = cap / 2;

		/* fill to capacity */
		for (uint32_t i = 0; i < cap; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		assert(ci_arr_len(a) == cap);

		/* shift half → offset=half, len=half */
		for (uint32_t i = 0; i < half; i++) {
			assert(ci_arr_shift(a) == IPTR(i));
		}
		assert(ci_arr_len(a) == cap - half);

		/* push half more (new values) → wraps buffer, back to full */
		for (uint32_t i = 0; i < half; i++) {
			assert(ci_arr_push(a, IPTR(cap + i)));
		}
		assert(ci_arr_len(a) == cap);
		assert(CI_IS_ARR_SMALL(a)); /* still inline */

		/* push one beyond → upgrade + linearize */
		assert(ci_arr_push(a, IPTR(cap + half)));
		assert(!CI_IS_ARR_SMALL(a)); /* upgraded */
		assert(ci_arr_len(a) == cap + 1);

		/* verify logical order: [half .. cap-1, cap .. cap+half] */
		for (uint32_t i = 0; i < cap - half; i++) {
			assert(ci_arr_index(a, i) == IPTR(half + i));
		}
		for (uint32_t i = 0; i <= half; i++) {
			assert(ci_arr_index(a, cap - half + i) == IPTR(cap + i));
		}
		ci_free(a);
	}

	/* --- Scenario 4: ensure_space(N) with large N on wrapped buffer --- */
	{
		ci_array *a = ci_arr_new(8);
		/* fill */
		for (int i = 0; i < 8; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		/* shift 4: offset=4, len=4 */
		for (int i = 0; i < 4; i++) {
			assert(ci_arr_shift(a) == IPTR(i));
		}
		/* push 4 more: offset=4, len=8, full and wrapped */
		for (int i = 8; i < 12; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		assert(ci_arr_len(a) == 8);
		assert(ci_arr_size(a) == 8);

		/* ensure 100 more slots → big realloc */
		assert(ci_arr_ensure_space(a, 100));
		assert(ci_arr_size(a) >= 108);
		assert(a->offset == 0); /* linearized */

		/* verify all 8 elements preserved: logical [4..11] */
		for (int i = 0; i < 8; i++) {
			assert(ci_arr_index(a, (uint32_t)i) == IPTR(4 + i));
		}
		ci_free(a);
	}

	teardown();
	printf("test_realloc_wrap: PASSED\n");
	return 0;
}
