/* test_inline.c — inline array specifics: pool sizes, capacity, flags, data pointer */
#include "ciobj.c"
#include <assert.h>
#include <stdio.h>
#include <stddef.h>

static void setup(void)    { ci_init(); ci_str_register(); ci_arr_register(); }
static void teardown(void) { ci_shutdown(); }

#define IPTR(i) ((ci_ptr)(uintptr_t)((uintptr_t)(i) * 8 + 8))

int main(void) {
	setup();

	size_t hdr      = offsetof(ci_array, inhdr_data);
	uint32_t cap128  = (128  - (uint32_t)hdr) / (uint32_t)sizeof(ci_ptr);
	uint32_t cap256  = (256  - (uint32_t)hdr) / (uint32_t)sizeof(ci_ptr);
	uint32_t cap1024 = (1024 - (uint32_t)hdr) / (uint32_t)sizeof(ci_ptr);
	uint32_t cap2048 = (2048 - (uint32_t)hdr) / (uint32_t)sizeof(ci_ptr);

	/* --- All 4 pool sizes: correct capacity reported --- */
	{
		ci_array *a128  = ci_arr_new_inline(1);
		ci_array *a256  = ci_arr_new_inline(cap128 + 1);
		ci_array *a1024 = ci_arr_new_inline(cap256 + 1);
		ci_array *a2048 = ci_arr_new_inline(cap1024 + 1);

		assert(ci_arr_size(a128)  == cap128);
		assert(ci_arr_size(a256)  == cap256);
		assert(ci_arr_size(a1024) == cap1024);
		assert(ci_arr_size(a2048) == cap2048);

		ci_free(a128); ci_free(a256); ci_free(a1024); ci_free(a2048);
	}

	/* --- Exact fit at boundary: request exactly cap128 → 128-byte slot --- */
	{
		ci_array *a = ci_arr_new_inline(cap128);
		assert(ci_arr_size(a) == cap128);
		ci_free(a);
	}

	/* --- One over cap128 → bumps to 256-byte slot --- */
	{
		ci_array *a = ci_arr_new_inline(cap128 + 1);
		assert(ci_arr_size(a) == cap256);
		ci_free(a);
	}

	/* --- One over cap2048 → NULL --- */
	{
		ci_array *a = ci_arr_new_inline(cap2048 + 1);
		assert(a == NULL);
	}

	/* --- Push to full capacity of each pool, verify all via index --- */
	{
		/* 128-byte pool */
		ci_array *a = ci_arr_new_inline(1);
		assert(ci_arr_size(a) == cap128);
		for (uint32_t i = 0; i < cap128; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		assert(ci_arr_len(a) == cap128);
		for (uint32_t i = 0; i < cap128; i++) {
			assert(ci_arr_index(a, i) == IPTR(i));
		}
		ci_free(a);
	}
	{
		/* 256-byte pool */
		ci_array *a = ci_arr_new_inline(cap128 + 1);
		assert(ci_arr_size(a) == cap256);
		for (uint32_t i = 0; i < cap256; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		assert(ci_arr_len(a) == cap256);
		for (uint32_t i = 0; i < cap256; i++) {
			assert(ci_arr_index(a, i) == IPTR(i));
		}
		ci_free(a);
	}

	/* --- Circular wrap within inline: shift some, push some, verify --- */
	{
		ci_array *a = ci_arr_new_inline(1); /* cap128 */
		uint32_t cap = ci_arr_size(a);
		assert(cap >= 4);

		/* fill */
		for (uint32_t i = 0; i < cap; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		/* shift 4 → offset=4, len=cap-4 */
		for (uint32_t i = 0; i < 4; i++) {
			assert(ci_arr_shift(a) == IPTR(i));
		}
		/* push 4 new values (wraps tail) */
		for (uint32_t i = 0; i < 4; i++) {
			assert(ci_arr_push(a, IPTR(cap + i)));
		}
		assert(ci_arr_len(a) == cap);

		/* verify logical order: [4 .. cap-1, cap .. cap+3] */
		for (uint32_t i = 0; i < cap - 4; i++) {
			assert(ci_arr_index(a, i) == IPTR(4 + i));
		}
		for (uint32_t i = 0; i < 4; i++) {
			assert(ci_arr_index(a, cap - 4 + i) == IPTR(cap + i));
		}
		ci_free(a);
	}

	/* --- data pointer always == inhdr_data while inline --- */
	{
		ci_array *a128  = ci_arr_new_inline(1);
		ci_array *a256  = ci_arr_new_inline(cap128 + 1);
		ci_array *a1024 = ci_arr_new_inline(cap256 + 1);
		ci_array *a2048 = ci_arr_new_inline(cap1024 + 1);

		assert(a128->data  == a128->inhdr_data);
		assert(a256->data  == a256->inhdr_data);
		assert(a1024->data == a1024->inhdr_data);
		assert(a2048->data == a2048->inhdr_data);

		ci_free(a128); ci_free(a256); ci_free(a1024); ci_free(a2048);
	}

	/* --- CI_OBJ_SMALL flag set on inline, cleared after upgrade --- */
	{
		ci_array *a = ci_arr_new_inline(1);
		assert(a->gc.flags & CI_OBJ_SMALL);
		assert(CI_IS_ARR_SMALL(a));

		uint32_t cap = ci_arr_size(a);
		/* fill to capacity */
		for (uint32_t i = 0; i < cap; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		assert(a->gc.flags & CI_OBJ_SMALL); /* still inline */

		/* push one more → upgrade */
		assert(ci_arr_push(a, IPTR(cap)));
		assert(!(a->gc.flags & CI_OBJ_SMALL)); /* cleared */
		assert(!CI_IS_ARR_SMALL(a));
		ci_free(a);
	}

	/* --- After upgrade: data != inhdr_data --- */
	{
		ci_array *a = ci_arr_new_inline(1);
		uint32_t cap = ci_arr_size(a);
		for (uint32_t i = 0; i < cap; i++) {
			assert(ci_arr_push(a, IPTR(i)));
		}
		assert(a->data == a->inhdr_data);

		assert(ci_arr_push(a, IPTR(cap)));
		assert(a->data != a->inhdr_data); /* heap-allocated now */
		ci_free(a);
	}

	/* --- Destructor: free inline array (no crash) --- */
	{
		ci_array *a = ci_arr_new_inline(4);
		assert(a != NULL);
		ci_arr_push(a, IPTR(0));
		ci_arr_push(a, IPTR(1));
		ci_free(a); /* must not crash */
	}

	/* --- Destructor after upgrade: frees malloc'd data (no leak) --- */
	{
		ci_array *a = ci_arr_new_inline(1);
		uint32_t cap = ci_arr_size(a);
		for (uint32_t i = 0; i <= cap; i++) {
			ci_arr_push(a, IPTR(i));
		}
		assert(!CI_IS_ARR_SMALL(a)); /* upgraded */
		ci_free(a); /* frees malloc'd data, must not crash */
	}

	teardown();
	printf("test_inline: PASSED\n");
	return 0;
}
