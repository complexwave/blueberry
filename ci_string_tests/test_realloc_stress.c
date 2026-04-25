/* test_realloc_stress.c — reallocation growth patterns */
#include "ciobj.c"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static void setup(void)    { ci_init(); ci_str_register(); }
static void teardown(void) { ci_shutdown(); }

int main(void) {
    setup();

    /* --- Start with size=1, append 1 byte at a time up to 10000: data integrity --- */
    {
        ci_str *s = ci_str_new(1);
        assert(s != NULL);

        for (int i = 0; i < 10000; i++) {
            char c = (char)('A' + i % 26);
            int ok = ci_str_append(s, &c, 1);
            assert(ok == 1);
        }

        assert(ci_str_len(s) == 10000);

        const uint8_t *p = ci_str_head(s);
        for (int i = 0; i < 10000; i++) {
            assert(p[i] == (uint8_t)('A' + i % 26));
        }

        ci_free(s);
    }

    /* --- Track ci_str_size() growth: should roughly double on each realloc --- */
    {
        ci_str *s = ci_str_new(1);
        assert(s != NULL);

        size_t prev_size = ci_str_size(s);
        int    doublings = 0;

        for (int i = 0; i < 256; i++) {
            char c = 'x';
            ci_str_append(s, &c, 1);
            size_t cur_size = ci_str_size(s);
            if (cur_size > prev_size) {
                /* should be >= double the old size */
                assert(cur_size >= prev_size * 2);
                doublings++;
                prev_size = cur_size;
            }
        }

        assert(doublings >= 1); /* at least one doubling happened */

        ci_free(s);
    }

    /* --- ensure_tail with large request: exact-fit (no doubling when request > 2x) --- */
    {
        ci_str *s = ci_str_new(4);
        assert(s != NULL);

        /* Fill the 4 bytes */
        ci_str_append(s, "ABCD", 4);

        /* Request much more than double (e.g. 1000 bytes) */
        uint8_t *p = ci_str_ensure_tail(s, 1000);
        assert(p != NULL);

        /* The implementation picks max(size*2, end_off+n).
         * end_off=4, n=1000, size*2=8.
         * max(8, 4+1000) = 1004. So newsize = 1004 (exact fit). */
        assert(ci_str_size(s) >= 1000 + 4);
        assert(ci_str_tail_space(s) >= 1000);

        ci_free(s);
    }

    /* --- Interleave ensure_head and ensure_tail: both sides stable --- */
    {
        ci_str *s = ci_str_new(64);
        assert(s != NULL);
        ci_str_append(s, "middle", 6);

        for (int i = 0; i < 20; i++) {
            uint8_t *h = ci_str_ensure_head(s, 4);
            assert(h != NULL);
            memcpy(h, "pre-", 4);

            uint8_t *t = ci_str_ensure_tail(s, 4);
            assert(t != NULL);
            memcpy(t, "suf+", 4);
            ci_str_put_tail(s, 4);
        }

        /* start must point to valid data */
        assert(ci_str_head(s) != NULL);
        assert(ci_str_len(s) > 0);

        ci_free(s);
    }

    /* --- After many reallocs, compact, verify data integrity --- */
    {
        ci_str *s = ci_str_new(1);
        assert(s != NULL);

        /* Build a known pattern */
        for (int i = 0; i < 500; i++) {
            char c = (char)('A' + i % 26);
            ci_str_append(s, &c, 1);
        }

        /* Create head space then compact */
        ci_str_ensure_head(s, 16);
        assert(ci_str_head_space(s) > 0);

        ci_str_compact(s);
        assert(ci_str_head_space(s) == 0);
        assert(ci_str_len(s) == 500);

        const uint8_t *p = ci_str_head(s);
        for (int i = 0; i < 500; i++) {
            assert(p[i] == (uint8_t)('A' + i % 26));
        }

        ci_free(s);
    }

    teardown();
    printf("test_realloc_stress: PASSED\n");
    return 0;
}
