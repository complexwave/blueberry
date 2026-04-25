/* test_put_tail.c — committing tail bytes */
#include "ciobj.c"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static void setup(void)    { ci_init(); ci_str_register(); }
static void teardown(void) { ci_shutdown(); }

int main(void) {
    setup();

    /* --- write 5 bytes then put_tail(5) → len increases by 5 --- */
    {
        ci_str *s = ci_str_new(16);
        assert(s != NULL);

        uint8_t *tail = ci_str_ensure_tail(s, 10);
        assert(tail != NULL);
        memcpy(tail, "hello", 5);
        ci_str_put_tail(s, 5);

        assert(ci_str_len(s) == 5);
        assert(memcmp(ci_str_head(s), "hello", 5) == 0);
        assert(ci_str_tail(s) == ci_str_head(s) + 5);

        ci_free(s);
    }

    /* --- put_tail resets hash --- */
    {
        ci_str *s = ci_str_from_cstr("test");
        assert(s != NULL);

        uint32_t h = ci_str_hash(s);
        assert(h != 0);

        uint8_t *tail = ci_str_ensure_tail(s, 4);
        assert(tail != NULL);
        memcpy(tail, "more", 4);
        ci_str_put_tail(s, 4);

        assert(s->hash == 0); /* hash reset */
        ci_free(s);
    }

    /* --- multiple put_tail calls accumulate --- */
    {
        ci_str *s = ci_str_new(32);
        assert(s != NULL);

        uint8_t *t;
        t = ci_str_ensure_tail(s, 3); memcpy(t, "aaa", 3); ci_str_put_tail(s, 3);
        t = ci_str_ensure_tail(s, 3); memcpy(t, "bbb", 3); ci_str_put_tail(s, 3);
        t = ci_str_ensure_tail(s, 3); memcpy(t, "ccc", 3); ci_str_put_tail(s, 3);

        assert(ci_str_len(s) == 9);
        assert(memcmp(ci_str_head(s), "aaabbbccc", 9) == 0);
        ci_free(s);
    }

    /* --- put_tail(0) → no change but still resets hash --- */
    {
        ci_str *s = ci_str_from_cstr("x");
        assert(s != NULL);

        uint32_t h = ci_str_hash(s);
        assert(h != 0);

        size_t old_len = ci_str_len(s);
        ci_str_put_tail(s, 0);

        assert(ci_str_len(s) == old_len);
        assert(s->hash == 0); /* reset even for put_tail(0) */

        ci_free(s);
    }

    teardown();
    printf("test_put_tail: PASSED\n");
    return 0;
}
