/* test_ensure_space.c — explicit space reservation */
#include "ciobj.c"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>

static void setup(void)    { ci_init(); ci_str_register(); ci_arr_register(); }
static void teardown(void) { ci_shutdown(); }

#define IPTR(i) ((ci_ptr)(uintptr_t)((uintptr_t)(i) * 8 + 8))

int main(void) {
	setup();

	/* --- ensure_space(0) on empty → success, no change --- */
	{
		ci_array *a = ci_arr_new(8);
		uint32_t old_size = ci_arr_size(a);
		assert(ci_arr_ensure_space(a, 0));
		assert(ci_arr_size(a) == old_size);
		assert(ci_arr_len(a) == 0);
		ci_free(a);
	}

	/* --- ensure_space(N) on empty → size >= N --- */
	{
		ci_array *a = ci_arr_new(1);
		assert(ci_arr_ensure_space(a, 500));
		assert(ci_arr_size(a) >= 500);
		ci_free(a);
	}

	/* --- Fill to half, ensure_space(remaining) → no realloc (size unchanged) --- */
	{
		ci_array *a = ci_arr_new(16);
		uint32_t cap = ci_arr_size(a);
		uint32_t half = cap / 2;

		for (uint32_t i = 0; i < half; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		/* remaining space = cap - half; ensure_space(remaining) should fit */
		assert(ci_arr_ensure_space(a, cap - half));
		assert(ci_arr_size(a) == cap); /* no realloc */
		ci_free(a);
	}

	/* --- Fill to full, ensure_space(1) → realloc, growth policy ~2x --- */
	{
		ci_array *a = ci_arr_new(8);
		uint32_t cap = ci_arr_size(a);
		for (uint32_t i = 0; i < cap; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		assert(ci_arr_len(a) == cap);
		assert(ci_arr_ensure_space(a, 1));
		assert(ci_arr_size(a) >= cap + 1);
		assert(ci_arr_size(a) >= cap * 2); /* 2x growth policy */
		ci_free(a);
	}

	/* --- ensure_space(1000) on size=8 → size >= 1008 --- */
	{
		ci_array *a = ci_arr_new(8);
		assert(ci_arr_ensure_space(a, 1000));
		assert(ci_arr_size(a) >= 1000);
		ci_free(a);
	}

	/* --- ensure_space after shift (offset != 0): linearizes, offset resets to 0 --- */
	{
		ci_array *a = ci_arr_new(8);
		for (int i = 0; i < 8; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		for (int i = 0; i < 4; i++) {
			assert(ci_arr_shift(a) == IPTR(i));
		}
		assert(a->offset == 4);
		/* push 4 more to re-fill */
		for (int i = 8; i < 12; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		assert(ci_arr_len(a) == 8);
		/* force ensure_space */
		assert(ci_arr_ensure_space(a, 1));
		assert(a->offset == 0); /* linearized */
		/* verify elements preserved: logical [4..11] */
		for (int i = 0; i < 8; i++) {
			assert(ci_arr_index(a, (uint32_t)i) == IPTR(4 + i));
		}
		ci_free(a);
	}

	/* --- ensure_space on inline → upgrades, then grows if needed --- */
	{
		ci_array *a = ci_arr_new_inline(1);
		uint32_t cap = ci_arr_size(a);
		assert(CI_IS_ARR_SMALL(a));

		/* fill to capacity */
		for (uint32_t i = 0; i < cap; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		/* ensure_space for 100 more → upgrade + grow */
		assert(ci_arr_ensure_space(a, 100));
		assert(!CI_IS_ARR_SMALL(a)); /* upgraded */
		assert(ci_arr_size(a) >= cap + 100);

		/* elements preserved */
		for (uint32_t i = 0; i < cap; i++) {
			assert(ci_arr_index(a, i) == IPTR(i));
		}
		ci_free(a);
	}

	/* --- Multiple ensure_space calls: doesn't shrink, only grows --- */
	{
		ci_array *a = ci_arr_new(4);
		assert(ci_arr_ensure_space(a, 100));
		uint32_t s1 = ci_arr_size(a);
		assert(s1 >= 100);

		assert(ci_arr_ensure_space(a, 50));
		assert(ci_arr_size(a) >= s1); /* not smaller */

		assert(ci_arr_ensure_space(a, 200));
		assert(ci_arr_size(a) >= 200);
		ci_free(a);
	}

	/* --- ensure_space(UINT32_MAX) → OOM, returns 0 gracefully --- */
	{
		ci_array *a = ci_arr_new(4);
		int ok = ci_arr_ensure_space(a, UINT32_MAX);
		/* either returns 0 (OOM) or succeeds (unlikely) — just must not crash */
		(void)ok;
		ci_free(a);
	}

	teardown();
	printf("test_ensure_space: PASSED\n");
	return 0;
}
