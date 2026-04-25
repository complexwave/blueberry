/* test_ensure_tail.c — tail space guarantee and realloc */
#include "ciobj.c"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static void setup(void)    { ci_init(); ci_str_register(); }
static void teardown(void) { ci_shutdown(); }

int main(void) {
    setup();

    /* --- ensure_tail(8) on size-8 string: no realloc, returns end ptr --- */
    {
        ci_str *s = ci_str_new(8);
        assert(s != NULL);
        assert(ci_str_tail_space(s) == 8);

        uint8_t *ptr = ci_str_ensure_tail(s, 8);
        assert(ptr != NULL);
        assert(ptr == s->end);

        ci_free(s);
    }

    /* --- fill then ensure_tail again → realloc triggered --- */
    {
        ci_str *s = ci_str_new(8);
        assert(s != NULL);

        uint8_t *w = ci_str_ensure_tail(s, 8);
        assert(w != NULL);
        memset(w, 'Z', 8);
        ci_str_put_tail(s, 8);
        assert(ci_str_len(s)        == 8);
        assert(ci_str_tail_space(s) == 0);

        /* data preserved after realloc */
        uint8_t *w2 = ci_str_ensure_tail(s, 8);
        assert(w2 != NULL);
        assert(ci_str_tail_space(s) >= 8);
        assert(memcmp(ci_str_head(s), "ZZZZZZZZ", 8) == 0);

        ci_free(s);
    }

    /* --- ensure_tail(0) always succeeds --- */
    {
        ci_str *s = ci_str_new(0);
        assert(s != NULL);
        uint8_t *p = ci_str_ensure_tail(s, 0);
        assert(p != NULL);
        ci_free(s);
    }

    /* --- ensure_tail(1MB) succeeds --- */
    {
        ci_str *s = ci_str_new(8);
        assert(s != NULL);
        uint8_t *p = ci_str_ensure_tail(s, 1024 * 1024);
        assert(p != NULL);
        assert(ci_str_tail_space(s) >= 1024 * 1024);
        ci_free(s);
    }

    /* --- doubling growth policy --- */
    {
        ci_str *s = ci_str_new(4);
        assert(s != NULL);

        /* fill all 4 bytes */
        uint8_t *w = ci_str_ensure_tail(s, 4);
        memset(w, 'A', 4);
        ci_str_put_tail(s, 4);

        size_t prev_size = ci_str_size(s); /* 4 */
        assert(prev_size == 4);

        /* trigger realloc: need 1 more byte */
        ci_str_ensure_tail(s, 1);
        size_t new_size = ci_str_size(s);
        /* doubling: new_size should be >= 2 * prev_size */
        assert(new_size >= prev_size * 2);

        ci_free(s);
    }

    /* --- ensure_tail does NOT reset hash --- */
    {
        ci_str *s = ci_str_new(8);
        assert(s != NULL);
        ci_str_append(s, "abc", 3);
        uint32_t h = ci_str_hash(s);
        assert(h != 0);

        /* trigger realloc via ensure_tail */
        uint8_t *p = ci_str_ensure_tail(s, 1024);
        assert(p != NULL);
        assert(s->hash == h); /* hash preserved */

        ci_free(s);
    }

    teardown();
    printf("test_ensure_tail: PASSED\n");
    return 0;
}
