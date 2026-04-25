/* test_upgrade.c — inline-to-full upgrade */
#include "ciobj.c"
#include <assert.h>
#include <stdio.h>

static void setup(void)    { ci_init(); ci_str_register(); ci_arr_register(); }
static void teardown(void) { ci_shutdown(); }

#define IPTR(i) ((ci_ptr)(uintptr_t)((uintptr_t)(i) * 8 + 8))

int main(void) {
	setup();

	/* --- Fill to capacity, push one more → triggers upgrade --- */
	{
		ci_array *a = ci_arr_new_inline(1);
		uint32_t cap = ci_arr_size(a);
		assert(CI_IS_ARR_SMALL(a));

		for (uint32_t i = 0; i < cap; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		assert(CI_IS_ARR_SMALL(a)); /* still inline */

		/* push one more → upgrade */
		assert(ci_arr_push(a, IPTR(cap)));
		assert(!CI_IS_ARR_SMALL(a)); /* CI_OBJ_SMALL cleared */

		/* all pre-upgrade elements preserved */
		assert(ci_arr_len(a) == cap + 1);
		for (uint32_t i = 0; i <= cap; i++) {
			assert(ci_arr_index(a, i) == IPTR(i));
		}
		ci_free(a);
	}

	/* --- data pointer changed after upgrade --- */
	{
		ci_array *a = ci_arr_new_inline(1);
		uint32_t cap = ci_arr_size(a);
		assert(a->data == a->inhdr_data);

		for (uint32_t i = 0; i < cap; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		assert(a->data == a->inhdr_data);

		assert(ci_arr_push(a, IPTR(cap)));
		assert(a->data != a->inhdr_data);
		ci_free(a);
	}

	/* --- Push/pop/shift/unshift all work after upgrade --- */
	{
		ci_array *a = ci_arr_new_inline(1);
		uint32_t cap = ci_arr_size(a);
		for (uint32_t i = 0; i < cap + 1; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		assert(!CI_IS_ARR_SMALL(a));

		/* push more */
		assert(ci_arr_push(a, IPTR(cap + 1)));
		assert(ci_arr_push(a, IPTR(cap + 2)));

		/* unshift at head */
		assert(ci_arr_unshift(a, IPTR(999)));
		assert(ci_arr_index(a, 0) == IPTR(999));

		/* pop from tail */
		assert(ci_arr_pop(a) == IPTR(cap + 2));

		/* shift from head */
		assert(ci_arr_shift(a) == IPTR(999));

		/* verify remaining elements are intact */
		assert(ci_arr_len(a) == cap + 2);
		for (uint32_t i = 0; i <= cap + 1; i++) {
			assert(ci_arr_index(a, i) == IPTR(i));
		}
		ci_free(a);
	}

	/* --- Upgrade of wrapped inline buffer: shift some, push some, then push beyond --- */
	{
		ci_array *a = ci_arr_new_inline(1);
		uint32_t cap = ci_arr_size(a);
		assert(cap >= 4);
		uint32_t half = cap / 2;

		/* fill to capacity */
		for (uint32_t i = 0; i < cap; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		/* shift half → wrap offset */
		for (uint32_t i = 0; i < half; i++) {
			assert(ci_arr_shift(a) == IPTR(i));
		}
		/* push half new values → physically wrapped, back to full */
		for (uint32_t i = 0; i < half; i++) {
			assert(ci_arr_push(a, IPTR(cap + i)));
		}
		assert(ci_arr_len(a) == cap);
		assert(CI_IS_ARR_SMALL(a)); /* still inline */

		/* push one more → upgrade, must linearize correctly */
		assert(ci_arr_push(a, IPTR(cap + half)));
		assert(!CI_IS_ARR_SMALL(a));
		assert(ci_arr_len(a) == cap + 1);

		/* verify logical order */
		for (uint32_t i = 0; i < cap - half; i++) {
			assert(ci_arr_index(a, i) == IPTR(half + i));
		}
		for (uint32_t i = 0; i <= half; i++) {
			assert(ci_arr_index(a, cap - half + i) == IPTR(cap + i));
		}
		ci_free(a);
	}

	/* --- Upgrade preserves logical order regardless of offset --- */
	{
		/* use largest inline pool */
		size_t hdr = offsetof(ci_array, inhdr_data);
		uint32_t cap = (1024 - (uint32_t)hdr) / (uint32_t)sizeof(ci_ptr);
		ci_array *a = ci_arr_new_inline(cap);
		assert(ci_arr_size(a) == cap);

		for (uint32_t i = 0; i < cap; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		/* shift a few to create offset */
		uint32_t shift_n = 7;
		for (uint32_t i = 0; i < shift_n; i++) {
			assert(ci_arr_shift(a) == IPTR(i));
		}
		/* push to fill back up */
		for (uint32_t i = 0; i < shift_n; i++) {
			assert(ci_arr_push(a, IPTR(cap + i)));
		}
		/* upgrade */
		assert(ci_arr_push(a, IPTR(cap + shift_n)));
		assert(!CI_IS_ARR_SMALL(a));
		assert(ci_arr_len(a) == cap + 1);

		/* logical: [shift_n .. cap-1, cap .. cap+shift_n] */
		for (uint32_t i = 0; i < cap - shift_n; i++) {
			assert(ci_arr_index(a, i) == IPTR(shift_n + i));
		}
		for (uint32_t i = 0; i <= shift_n; i++) {
			assert(ci_arr_index(a, cap - shift_n + i) == IPTR(cap + i));
		}
		ci_free(a);
	}

	/* --- Multiple operations after upgrade (normal full array) --- */
	{
		ci_array *a = ci_arr_new_inline(1);
		uint32_t cap = ci_arr_size(a);
		for (uint32_t i = 0; i <= cap; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		assert(!CI_IS_ARR_SMALL(a));

		/* push 100 more, pop 50, shift 25 */
		for (int i = 0; i < 100; i++) {
			assert(ci_arr_push(a, IPTR(1000 + i)));
		}
		for (int i = 0; i < 50; i++) {
			ci_arr_pop(a);
		}
		for (int i = 0; i < 25; i++) {
			ci_arr_shift(a);
		}
		/* length: (cap+1) + 100 - 50 - 25 = cap + 26 */
		assert(ci_arr_len(a) == cap + 26);
		ci_free(a);
	}

	/* --- Upgrade of empty inline array → works, len=0 --- */
	{
		/* Can't directly trigger upgrade of empty (push beyond full won't happen).
		 * But ci_arr_ensure_space(1) on a full empty inline triggers upgrade. */
		ci_array *a = ci_arr_new_inline(1);
		uint32_t cap = ci_arr_size(a);
		assert(ci_arr_len(a) == 0);

		/* fill to capacity then clear — simulates empty-at-capacity */
		for (uint32_t i = 0; i < cap; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		ci_arr_clear(a);
		assert(ci_arr_len(a) == 0);
		assert(CI_IS_ARR_SMALL(a));

		/* pushing beyond capacity now upgrades */
		for (uint32_t i = 0; i <= cap; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		assert(!CI_IS_ARR_SMALL(a));
		assert(ci_arr_len(a) == cap + 1);
		for (uint32_t i = 0; i <= cap; i++) {
			assert(ci_arr_index(a, i) == IPTR(i));
		}
		ci_free(a);
	}

	teardown();
	printf("test_upgrade: PASSED\n");
	return 0;
}
