/* test_rmhead.c — ci_str_rmhead with buffer-drain reset */
#include "ciobj.c"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static void setup(void)    { ci_init(); ci_str_register(); }
static void teardown(void) { ci_shutdown(); }

int main(void) {
    setup();

    /* --- rmhead(3) on "hello" → len=2, data is "lo" --- */
    {
        ci_str *s = ci_str_from_cstr("hello");
        assert(s != NULL);
        size_t removed = ci_str_rmhead(s, 3);
        assert(removed == 3);
        assert(ci_str_len(s) == 2);
        assert(memcmp(ci_str_head(s), "lo", 2) == 0);
        ci_free(s);
    }

    /* --- rmhead(0) → no change --- */
    {
        ci_str *s = ci_str_from_cstr("hello");
        size_t removed = ci_str_rmhead(s, 0);
        assert(removed == 0);
        assert(ci_str_len(s) == 5);
        assert(memcmp(ci_str_head(s), "hello", 5) == 0);
        ci_free(s);
    }

    /* --- rmhead(100) on len=5 → clamped to 5, returns 5, len=0 --- */
    {
        ci_str *s = ci_str_from_cstr("hello");
        size_t removed = ci_str_rmhead(s, 100);
        assert(removed == 5);
        assert(ci_str_len(s) == 0);
        ci_free(s);
    }

    /* --- rmhead increases head_space (when not fully drained) --- */
    {
        ci_str *s = ci_str_from_cstr("hello");
        assert(ci_str_head_space(s) == 0);
        ci_str_rmhead(s, 3);
        assert(ci_str_head_space(s) == 3);
        assert(ci_str_len(s) == 2);
        ci_free(s);
    }

    /* --- rmhead resets hash --- */
    {
        ci_str *s = ci_str_from_cstr("hello");
        uint32_t h = ci_str_hash(s);
        assert(h != 0);
        ci_str_rmhead(s, 1);
        assert(s->hash == 0);
        ci_free(s);
    }

    /* --- DRAIN RESET: rmhead(len) → start==end==memory, head_space==0 --- */
    {
        ci_str *s = ci_str_from_cstr("drain me");
        assert(s != NULL);
        size_t len = ci_str_len(s);

        size_t removed = ci_str_rmhead(s, len);
        assert(removed == len);
        assert(ci_str_len(s) == 0);
        assert(s->start == s->memory);
        assert(s->end   == s->memory);
        assert(ci_str_head_space(s) == 0);

        ci_free(s);
    }

    /* --- DRAIN RESET: rmhead exactly consuming last bytes --- */
    {
        ci_str *s = ci_str_new(64);
        assert(s != NULL);
        ci_str_append(s, "abcde", 5);
        ci_str_rmhead(s, 3); /* consume 3: "de" remains, start drifted */
        assert(ci_str_head_space(s) == 3);

        /* consume the remaining 2 */
        size_t removed = ci_str_rmhead(s, 2);
        assert(removed == 2);
        assert(s->start == s->memory);
        assert(s->end   == s->memory);
        assert(ci_str_head_space(s) == 0);

        ci_free(s);
    }

    teardown();
    printf("test_rmhead: PASSED\n");
    return 0;
}
