/* test_hash.c — ci_str_hash FNV-1a */
#include "ciobj.c"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static void setup(void)    { ci_init(); ci_str_register(); }
static void teardown(void) { ci_shutdown(); }

/* Compute FNV-1a manually for verification */
static uint32_t fnv1a(const char *data, size_t len) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        h ^= (uint32_t)(unsigned char)data[i];
        h *= 16777619u;
    }
    return h;
}

int main(void) {
    setup();

    /* --- hash of "hello": consistent, cached, matches FNV-1a --- */
    {
        ci_str *s = ci_str_from_cstr("hello");
        assert(s != NULL);
        assert(s->hash == 0); /* not yet computed */

        uint32_t h1 = ci_str_hash(s);
        assert(h1 != 0);
        assert(s->hash == h1); /* cached */

        uint32_t h2 = ci_str_hash(s);
        assert(h2 == h1); /* same value */

        uint32_t expected = fnv1a("hello", 5);
        assert(h1 == (expected ? expected : 1));

        ci_free(s);
    }

    /* --- two strings with same content → same hash --- */
    {
        ci_str *a = ci_str_from_cstr("same content");
        ci_str *b = ci_str_from_cstr("same content");
        assert(ci_str_hash(a) == ci_str_hash(b));
        ci_free(a);
        ci_free(b);
    }

    /* --- two strings with different content → different hash --- */
    {
        ci_str *a = ci_str_from_cstr("content A");
        ci_str *b = ci_str_from_cstr("content B");
        assert(ci_str_hash(a) != ci_str_hash(b));
        ci_free(a);
        ci_free(b);
    }

    /* --- empty string hash = FNV offset basis = 2166136261 --- */
    {
        ci_str *s = ci_str_new(8);
        assert(s != NULL);
        /* len=0: loop doesn't run, h stays at offset basis */
        uint32_t h = ci_str_hash(s);
        assert(h == 2166136261u);
        assert(s->hash == 2166136261u);
        ci_free(s);
    }

    /* --- hash is always nonzero (sentinel: 0→1) --- */
    {
        /* Verify the sentinel path exists in code by checking that ci_str_hash
         * never returns 0. We test many strings; and verify the code:
         * if FNV naturally yields 0, library stores 1. */
        const char *samples[] = { "a", "b", "abc", "12345", "hello world", "" };
        for (int i = 0; i < 6; i++) {
            ci_str *s = ci_str_from_cstr(samples[i]);
            if (ci_str_len(s) == 0) {
                ci_str_clear(s);
            }
            uint32_t h = ci_str_hash(s);
            assert(h != 0);
            ci_free(s);
        }
    }

    /* --- reset_hash clears, then re-hash gives same result --- */
    {
        ci_str *s = ci_str_from_cstr("consistent");
        uint32_t h1 = ci_str_hash(s);
        ci_str_reset_hash(s);
        assert(s->hash == 0);
        uint32_t h2 = ci_str_hash(s);
        assert(h1 == h2);
        ci_free(s);
    }

    teardown();
    printf("test_hash: PASSED\n");
    return 0;
}
