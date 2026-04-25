/* test_basic.c — ci_map fundamentals: new, tags, set/get/delete, clear, iterate */
#include "ciobj.c"
#include <assert.h>
#include <stdio.h>

static void setup(void)    { ci_init(); ci_str_register(); ci_arr_register(); ci_map_register(); }
static void teardown(void) { ci_shutdown(); }

/* sentinel pointers — never dereferenced, just compared */
static int dummy[64];
#define PTR(i) ((ci_ptr)&dummy[i])

int main(void) {
	setup();

	/* --- ci_map_new: allocation and tags --- */
	{
		ci_map *m = ci_map_new(16);
		assert(m != NULL);
		assert(CI_IS_ANY_MAP(m));
		assert(CI_IS_MAP(m));
		assert(ci_is_refcountable(m));
		assert(ci_refcnt(m) == 1);
		assert(ci_map_len(m) == 0);
		assert(ci_map_buckets(m) >= 16);
		ci_free(m);
	}

	/* --- ci_map_new(0) and (1) clamp to MIN_BUCKETS --- */
	{
		ci_map *m0 = ci_map_new(0);
		ci_map *m1 = ci_map_new(1);
		assert(m0 != NULL && m1 != NULL);
		assert(ci_map_buckets(m0) == CI_MAP_MIN_BUCKETS);
		assert(ci_map_buckets(m1) == CI_MAP_MIN_BUCKETS);
		ci_free(m0);
		ci_free(m1);
	}

	/* --- set / get roundtrip --- */
	{
		ci_map *m = ci_map_new(8);
		assert(ci_map_set(m, PTR(0), PTR(10)));
		assert(ci_map_set(m, PTR(1), PTR(11)));
		assert(ci_map_set(m, PTR(2), PTR(12)));

		assert(ci_map_get(m, PTR(0)) == PTR(10));
		assert(ci_map_get(m, PTR(1)) == PTR(11));
		assert(ci_map_get(m, PTR(2)) == PTR(12));
		assert(ci_map_len(m) == 3);

		/* missing key → NULL */
		assert(ci_map_get(m, PTR(5)) == NULL);

		ci_free(m);
	}

	/* --- set same key twice → replace, used unchanged --- */
	{
		ci_map *m = ci_map_new(8);
		ci_map_set(m, PTR(0), PTR(10));
		assert(ci_map_len(m) == 1);
		ci_map_set(m, PTR(0), PTR(20));
		assert(ci_map_len(m) == 1);
		assert(ci_map_get(m, PTR(0)) == PTR(20));
		ci_free(m);
	}

	/* --- delete: removes key, returns 1; missing returns 0 --- */
	{
		ci_map *m = ci_map_new(8);
		ci_map_set(m, PTR(0), PTR(10));
		ci_map_set(m, PTR(1), PTR(11));
		ci_map_set(m, PTR(2), PTR(12));

		assert(ci_map_delete(m, PTR(1)) == 1);
		assert(ci_map_len(m) == 2);
		assert(ci_map_get(m, PTR(1)) == NULL);
		/* neighbours still intact */
		assert(ci_map_get(m, PTR(0)) == PTR(10));
		assert(ci_map_get(m, PTR(2)) == PTR(12));

		/* delete missing → 0 */
		assert(ci_map_delete(m, PTR(1)) == 0);
		assert(ci_map_delete(m, PTR(9)) == 0);

		ci_free(m);
	}

	/* --- clear: used=0, all keys gone --- */
	{
		ci_map *m = ci_map_new(8);
		for (int i = 0; i < 8; i++) {
			ci_map_set(m, PTR(i), PTR(i + 20));
		}
		assert(ci_map_len(m) == 8);
		ci_map_clear(m);
		assert(ci_map_len(m) == 0);
		for (int i = 0; i < 8; i++) {
			assert(ci_map_get(m, PTR(i)) == NULL);
		}
		/* reusable after clear */
		ci_map_set(m, PTR(0), PTR(63));
		assert(ci_map_get(m, PTR(0)) == PTR(63));
		ci_free(m);
	}

	/* --- iteration: ci_map_next visits exactly used entries --- */
	{
		ci_map *m = ci_map_new(8);
		for (int i = 0; i < 10; i++) {
			ci_map_set(m, PTR(i), PTR(i + 30));
		}
		assert(ci_map_len(m) == 10);

		uint32_t cursor = 0;
		uint32_t count  = 0;
		ci_map_kv *kv;
		while ((kv = ci_map_next(m, &cursor)) != NULL) {
			/* value should be key + 30 (PTR(i) → PTR(i+30)) */
			int idx = (int)((int *)kv->key - dummy);
			assert(kv->val == PTR(idx + 30));
			count++;
		}
		assert(count == 10);
		ci_free(m);
	}

	/* --- resize: inserts beyond load limit, all entries survive --- */
	{
		ci_map *m = ci_map_new(8);
		/* insert 32 entries — well past initial 8*0.7=5 limit */
		for (int i = 0; i < 32; i++) {
			assert(ci_map_set(m, PTR(i), PTR(i + 40)));
		}
		assert(ci_map_len(m) == 32);
		for (int i = 0; i < 32; i++) {
			assert(ci_map_get(m, PTR(i)) == PTR(i + 40));
		}
		ci_free(m);
	}

	/* --- refcount on map itself --- */
	{
		ci_map *m = ci_map_new(8);
		assert(ci_refcnt(m) == 1);
		ci_inc(m);
		assert(ci_refcnt(m) == 2);
		assert(ci_dec(m) == 0); /* 2→1, not freed */
		assert(ci_dec(m) == 1); /* 1→0, freed + destructor */
	}

	teardown();
	printf("test_basic: PASSED\n");
	return 0;
}
