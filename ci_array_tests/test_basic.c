/* test_basic.c — ci_array fundamentals: new, inline, push/pop, shift/unshift, index, clear */
#include "ciobj.c"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static void setup(void)    { ci_init(); ci_str_register(); ci_arr_register(); }
static void teardown(void) { ci_shutdown(); }

/* sentinel pointers for testing — never dereferenced, just compared */
static int dummy[32];
#define PTR(i) ((ci_ptr)&dummy[i])

int main(void) {
	setup();

	/* --- ci_arr_new: basic allocation --- */
	{
		ci_array *a = ci_arr_new(16);
		assert(a != NULL);
		assert(ci_arr_len(a) == 0);
		assert(ci_arr_size(a) >= 16);
		assert(CI_IS_ANY_ARR(a));
		assert(CI_IS_ARR(a));
		assert(!CI_IS_ARR_SMALL(a));
		assert(ci_is_refcountable(a));
		assert(ci_refcnt(a) == 1);
		ci_free(a);
	}

	/* --- ci_arr_new(0): edge case --- */
	{
		ci_array *a = ci_arr_new(0);
		assert(a != NULL);
		assert(ci_arr_size(a) >= 1);
		ci_free(a);
	}

	/* --- ci_arr_new_inline: all 4 pool sizes --- */
	{
		/* small inline: should fit in 128-byte slot */
		ci_array *a = ci_arr_new_inline(1);
		assert(a != NULL);
		assert(CI_IS_ARR_SMALL(a));
		assert(!CI_IS_ARR(a));  /* small, not full */
		assert(CI_IS_ANY_ARR(a));
		assert(ci_is_refcountable(a));  /* all arrays are refcountable */
		assert(ci_arr_len(a) == 0);
		assert(ci_arr_size(a) > 0);
		/* data points to inhdr_data */
		assert(a->data == a->inhdr_data);
		ci_free(a);
	}

	/* --- inline capacity calculation --- */
	{
		size_t hdr = offsetof(ci_array, inhdr_data);
		uint32_t cap128  = (128  - (uint32_t)hdr) / (uint32_t)sizeof(ci_ptr);
		uint32_t cap256  = (256  - (uint32_t)hdr) / (uint32_t)sizeof(ci_ptr);
		uint32_t cap1024 = (1024 - (uint32_t)hdr) / (uint32_t)sizeof(ci_ptr);
		uint32_t cap2048 = (2048 - (uint32_t)hdr) / (uint32_t)sizeof(ci_ptr);

		ci_array *a1 = ci_arr_new_inline(1);
		assert(ci_arr_size(a1) == cap128);

		ci_array *a2 = ci_arr_new_inline(cap128 + 1);
		assert(ci_arr_size(a2) == cap256);

		ci_array *a3 = ci_arr_new_inline(cap256 + 1);
		assert(ci_arr_size(a3) == cap1024);

		ci_array *a4 = ci_arr_new_inline(cap1024 + 1);
		assert(ci_arr_size(a4) == cap2048);

		/* over max → NULL */
		ci_array *a5 = ci_arr_new_inline(cap2048 + 1);
		assert(a5 == NULL);

		ci_free(a1); ci_free(a2); ci_free(a3); ci_free(a4);
	}

	/* --- push / pop (tail) --- */
	{
		ci_array *a = ci_arr_new(8);
		for (int i = 0; i < 8; i++) {
			assert(ci_arr_push(a, PTR(i)));
		}
		assert(ci_arr_len(a) == 8);

		/* verify order: LIFO from pop */
		for (int i = 7; i >= 0; i--) {
			ci_ptr v = ci_arr_pop(a);
			assert(v == PTR(i));
		}
		assert(ci_arr_len(a) == 0);

		/* pop on empty → NULL */
		assert(ci_arr_pop(a) == NULL);
		ci_free(a);
	}

	/* --- unshift / shift (head) --- */
	{
		ci_array *a = ci_arr_new(8);
		for (int i = 0; i < 8; i++) {
			assert(ci_arr_unshift(a, PTR(i)));
		}
		assert(ci_arr_len(a) == 8);

		/* shift returns in FIFO order (most recent unshift first) */
		for (int i = 7; i >= 0; i--) {
			ci_ptr v = ci_arr_shift(a);
			assert(v == PTR(i));
		}
		assert(ci_arr_len(a) == 0);

		/* shift on empty → NULL */
		assert(ci_arr_shift(a) == NULL);
		ci_free(a);
	}

	/* --- index / set --- */
	{
		ci_array *a = ci_arr_new(8);
		for (int i = 0; i < 8; i++) {
			ci_arr_push(a, PTR(i));
		}

		/* random access */
		for (int i = 0; i < 8; i++) {
			assert(ci_arr_index(a, (uint32_t)i) == PTR(i));
		}

		/* set overwrites */
		ci_arr_set(a, 3, PTR(30));
		assert(ci_arr_index(a, 3) == PTR(30));
		/* others unchanged */
		assert(ci_arr_index(a, 2) == PTR(2));
		assert(ci_arr_index(a, 4) == PTR(4));

		ci_free(a);
	}

	/* --- mixed push and unshift with index --- */
	{
		ci_array *a = ci_arr_new(16);
		/* push 0,1,2 at tail, unshift 10,11,12 at head */
		ci_arr_push(a, PTR(0));
		ci_arr_push(a, PTR(1));
		ci_arr_push(a, PTR(2));
		ci_arr_unshift(a, PTR(10));
		ci_arr_unshift(a, PTR(11));
		ci_arr_unshift(a, PTR(12));
		/* logical order: 12 11 10 0 1 2 */
		assert(ci_arr_len(a) == 6);
		assert(ci_arr_index(a, 0) == PTR(12));
		assert(ci_arr_index(a, 1) == PTR(11));
		assert(ci_arr_index(a, 2) == PTR(10));
		assert(ci_arr_index(a, 3) == PTR(0));
		assert(ci_arr_index(a, 4) == PTR(1));
		assert(ci_arr_index(a, 5) == PTR(2));

		/* pop from tail → 2, shift from head → 12 */
		assert(ci_arr_pop(a) == PTR(2));
		assert(ci_arr_shift(a) == PTR(12));
		assert(ci_arr_len(a) == 4);
		/* remaining: 11 10 0 1 */
		assert(ci_arr_index(a, 0) == PTR(11));
		assert(ci_arr_index(a, 3) == PTR(1));

		ci_free(a);
	}

	/* --- clear --- */
	{
		ci_array *a = ci_arr_new(8);
		ci_arr_push(a, PTR(0));
		ci_arr_push(a, PTR(1));
		ci_arr_push(a, PTR(2));
		ci_arr_clear(a);
		assert(ci_arr_len(a) == 0);
		assert(a->offset == 0);
		/* can reuse after clear */
		ci_arr_push(a, PTR(5));
		assert(ci_arr_len(a) == 1);
		assert(ci_arr_index(a, 0) == PTR(5));
		ci_free(a);
	}

	/* --- inline push / pop --- */
	{
		ci_array *a = ci_arr_new_inline(4);
		uint32_t cap = ci_arr_size(a);
		assert(cap >= 4);

		ci_arr_push(a, PTR(0));
		ci_arr_push(a, PTR(1));
		ci_arr_push(a, PTR(2));
		ci_arr_push(a, PTR(3));
		assert(ci_arr_len(a) == 4);
		assert(ci_arr_index(a, 0) == PTR(0));
		assert(ci_arr_index(a, 3) == PTR(3));

		assert(ci_arr_pop(a) == PTR(3));
		assert(ci_arr_shift(a) == PTR(0));
		assert(ci_arr_len(a) == 2);

		ci_free(a);
	}

	/* --- refcount on full array --- */
	{
		ci_array *a = ci_arr_new(4);
		assert(ci_refcnt(a) == 1);
		ci_inc(a);
		assert(ci_refcnt(a) == 2);
		assert(ci_dec(a) == 0); /* refcnt=1, not freed */
		assert(ci_dec(a) == 1); /* refcnt=0, freed */
	}

	/* --- ensure_space triggers growth on full array --- */
	{
		ci_array *a = ci_arr_new(4);
		ci_arr_push(a, PTR(0));
		ci_arr_push(a, PTR(1));
		ci_arr_push(a, PTR(2));
		ci_arr_push(a, PTR(3));
		assert(ci_arr_len(a) == 4);
		assert(ci_arr_size(a) == 4);

		/* push beyond capacity → triggers ensure_space */
		assert(ci_arr_push(a, PTR(4)));
		assert(ci_arr_len(a) == 5);
		assert(ci_arr_size(a) >= 5);
		/* all elements preserved */
		for (int i = 0; i < 5; i++) {
			assert(ci_arr_index(a, (uint32_t)i) == PTR(i));
		}
		ci_free(a);
	}

	/* --- ensure_space triggers upgrade on inline array --- */
	{
		ci_array *a = ci_arr_new_inline(4);
		uint32_t cap = ci_arr_size(a);
		assert(CI_IS_ARR_SMALL(a));

		/* fill to capacity */
		for (uint32_t i = 0; i < cap; i++) {
			ci_arr_push(a, PTR(i % 32));
		}
		assert(ci_arr_len(a) == cap);

		/* push one more → upgrade from inline to full */
		assert(ci_arr_push(a, PTR(31)));
		assert(!CI_IS_ARR_SMALL(a)); /* upgraded */
		assert(ci_arr_len(a) == cap + 1);

		/* all elements preserved */
		for (uint32_t i = 0; i < cap; i++) {
			assert(ci_arr_index(a, i) == PTR(i % 32));
		}
		assert(ci_arr_index(a, cap) == PTR(31));
		ci_free(a);
	}

	teardown();
	printf("test_basic: PASSED\n");
	return 0;
}
