/* test_clear.c — clear semantics */
#include "ciobj.c"
#include <assert.h>
#include <stdio.h>

static void setup(void)    { ci_init(); ci_str_register(); ci_arr_register(); }
static void teardown(void) { ci_shutdown(); }

#define IPTR(i) ((ci_ptr)(uintptr_t)((uintptr_t)(i) * 8 + 8))

int main(void) {
	setup();

	/* --- Clear full array: len=0, offset=0, size unchanged --- */
	{
		ci_array *a = ci_arr_new(8);
		uint32_t old_size = ci_arr_size(a);
		for (int i = 0; i < 8; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		ci_arr_clear(a);
		assert(ci_arr_len(a) == 0);
		assert(a->offset == 0);
		assert(ci_arr_size(a) == old_size);
		ci_free(a);
	}

	/* --- Clear empty array: no-op, no crash --- */
	{
		ci_array *a = ci_arr_new(8);
		uint32_t old_size = ci_arr_size(a);
		ci_arr_clear(a);
		assert(ci_arr_len(a) == 0);
		assert(a->offset == 0);
		assert(ci_arr_size(a) == old_size);
		ci_free(a);
	}

	/* --- Clear wrapped array (offset != 0): offset resets to 0 --- */
	{
		ci_array *a = ci_arr_new(8);
		for (int i = 0; i < 8; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		for (int i = 0; i < 5; i++) {
			assert(ci_arr_shift(a) == IPTR(i));
		}
		assert(a->offset == 5);
		assert(ci_arr_len(a) == 3);
		ci_arr_clear(a);
		assert(ci_arr_len(a) == 0);
		assert(a->offset == 0);
		ci_free(a);
	}

	/* --- Clear inline array: len=0, offset=0, data still == inhdr_data --- */
	{
		ci_array *a = ci_arr_new_inline(4);
		assert(CI_IS_ARR_SMALL(a));
		uint32_t cap = ci_arr_size(a);
		for (uint32_t i = 0; i < cap; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		ci_arr_clear(a);
		assert(ci_arr_len(a) == 0);
		assert(a->offset == 0);
		assert(a->data == a->inhdr_data); /* still inline */
		assert(CI_IS_ARR_SMALL(a));
		ci_free(a);
	}

	/* --- After clear: push works, starts from offset=0 --- */
	{
		ci_array *a = ci_arr_new(8);
		for (int i = 0; i < 8; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		ci_arr_clear(a);
		assert(ci_arr_push(a, IPTR(42)));
		assert(ci_arr_len(a) == 1);
		assert(ci_arr_index(a, 0) == IPTR(42));
		assert(a->offset == 0);
		ci_free(a);
	}

	/* --- Clear doesn't free backing store (data pointer unchanged after clear) --- */
	{
		ci_array *a = ci_arr_new(8);
		for (int i = 0; i < 8; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		ci_ptr *data_before = a->data;
		uint32_t size_before = ci_arr_size(a);
		ci_arr_clear(a);
		assert(a->data == data_before);
		assert(ci_arr_size(a) == size_before);
		ci_free(a);
	}

	teardown();
	printf("test_clear: PASSED\n");
	return 0;
}
