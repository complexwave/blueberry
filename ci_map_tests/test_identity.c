/* test_identity.c — ci_map object-identity API: put/find/find_kv_hash/remove */
#include "ciobj.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void setup(void)    { ci_init(); ci_str_register(); ci_arr_register(); ci_map_register(); }
static void teardown(void) { ci_shutdown(); }

static int dummy[64];
#define PTR(i) ((ci_ptr)&dummy[i])

/* Build a ci_str from a C string literal */
static ci_str *make_str(const char *s) {
	ci_str *r = ci_str_from_cstr(s);
	assert(r != NULL);
	return r;
}

int main(void) {
	setup();

	/* --- 50-string content-based lookup ---
	 * Insert 50 ci_str keys mapped to PTR(i).
	 * Then create 50 fresh ci_str objects with identical content
	 * and verify ci_map_find returns the original values.
	 */
	{
		ci_map *m = ci_map_ident_new(64);
		assert(m != NULL);

		ci_str *keys[50];
		char buf[32];
		for (int i = 0; i < 50; i++) {
			snprintf(buf, sizeof(buf), "key_%02d", i);
			keys[i] = make_str(buf);
			assert(ci_map_put(m, keys[i], PTR(i)));
		}
		assert(ci_map_len(m) == 50);

		/* lookup with 50 new strings that have the same content */
		for (int i = 0; i < 50; i++) {
			snprintf(buf, sizeof(buf), "key_%02d", i);
			ci_str *lookup_key = make_str(buf);

			/* must be a different pointer */
			assert((ci_ptr)lookup_key != (ci_ptr)keys[i]);

			ci_ptr val = ci_map_find(m, lookup_key);
			assert(val == PTR(i));

			ci_free(lookup_key);
		}

		/* free inserted keys and map */
		for (int i = 0; i < 50; i++) ci_free(keys[i]);
		ci_free(m);
	}

	/* --- ci_map_put: replace existing key by content --- */
	{
		ci_map *m = ci_map_ident_new(8);

		ci_str *k1 = make_str("hello");
		ci_str *k2 = make_str("hello"); /* same content, different ptr */
		assert((ci_ptr)k1 != (ci_ptr)k2);

		ci_map_put(m, k1, PTR(10));
		assert(ci_map_len(m) == 1);

		/* re-inserting via k2 (same content) replaces, len unchanged */
		ci_map_put(m, k2, PTR(20));
		assert(ci_map_len(m) == 1);
		assert(ci_map_find(m, k1) == PTR(20));

		ci_free(k1);
		ci_free(k2);
		ci_free(m);
	}

	/* --- ci_map_find: miss returns NULL --- */
	{
		ci_map *m = ci_map_ident_new(8);
		ci_str *k = make_str("present");
		ci_map_put(m, k, PTR(5));

		ci_str *absent = make_str("absent");
		assert(ci_map_find(m, absent) == NULL);

		ci_free(absent);
		ci_free(k);
		ci_free(m);
	}

	/* --- ci_map_find_kv_hash: lookup with pre-computed hash --- */
	{
		ci_map *m = ci_map_ident_new(8);
		ci_str *k = make_str("hashme");
		ci_map_put(m, k, PTR(7));

		uint32_t h = m->hashcmp(k, NULL);

		/* lookup with same key + pre-computed hash */
		ci_map_kv *kv = ci_map_find_kv_hash(m, k, h);
		assert(kv != NULL);
		assert(kv->val == PTR(7));

		/* lookup with different pointer, same content, same hash */
		ci_str *k2 = make_str("hashme");
		uint32_t h2 = m->hashcmp(k2, NULL);
		assert(h == h2);
		ci_map_kv *kv2 = ci_map_find_kv_hash(m, k2, h2);
		assert(kv2 != NULL);
		assert(kv2->val == PTR(7));

		/* miss */
		ci_str *absent = make_str("other");
		assert(ci_map_find_kv_hash(m, absent, m->hashcmp(absent, NULL)) == NULL);

		ci_free(absent);
		ci_free(k2);
		ci_free(k);
		ci_free(m);
	}

	/* --- ci_map_remove: removes by content, returns 1; missing returns 0 --- */
	{
		ci_map *m = ci_map_ident_new(8);
		ci_str *k_a = make_str("alpha");
		ci_str *k_b = make_str("beta");
		ci_str *k_c = make_str("gamma");
		ci_map_put(m, k_a, PTR(1));
		ci_map_put(m, k_b, PTR(2));
		ci_map_put(m, k_c, PTR(3));
		assert(ci_map_len(m) == 3);

		/* remove "beta" via a fresh key with same content */
		ci_str *k_b2 = make_str("beta");
		assert((ci_ptr)k_b2 != (ci_ptr)k_b);
		assert(ci_map_remove(m, k_b2) == 1);
		assert(ci_map_len(m) == 2);
		assert(ci_map_find(m, k_b) == NULL);

		/* neighbours intact */
		assert(ci_map_find(m, k_a) == PTR(1));
		assert(ci_map_find(m, k_c) == PTR(3));

		/* double-remove → 0 */
		assert(ci_map_remove(m, k_b2) == 0);

		/* remove absent key → 0 */
		ci_str *k_absent = make_str("nope");
		assert(ci_map_remove(m, k_absent) == 0);

		ci_free(k_absent);
		ci_free(k_b2);
		ci_free(k_a);
		ci_free(k_b);
		ci_free(k_c);
		ci_free(m);
	}

	/* --- mixed sizes: strings spanning small/full pools --- */
	{
		ci_map *m = ci_map_ident_new(16);

		/* short key (fits small pool) */
		ci_str *short_k = make_str("x");
		/* long key (forces full ci_str) */
		char long_buf[200];
		memset(long_buf, 'a', sizeof(long_buf) - 1);
		long_buf[sizeof(long_buf) - 1] = '\0';
		ci_str *long_k = make_str(long_buf);

		ci_map_put(m, short_k, PTR(0));
		ci_map_put(m, long_k,  PTR(1));
		assert(ci_map_len(m) == 2);

		ci_str *short_k2 = make_str("x");
		ci_str *long_k2  = make_str(long_buf);
		assert(ci_map_find(m, short_k2) == PTR(0));
		assert(ci_map_find(m, long_k2)  == PTR(1));

		ci_free(short_k2);
		ci_free(long_k2);
		ci_free(short_k);
		ci_free(long_k);
		ci_free(m);
	}

	teardown();
	printf("test_identity: PASSED\n");
	return 0;
}
