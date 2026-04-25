/* test_prepend.c — ci_str_prepend head insertion */
#include "ciobj.c"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static void setup(void)    { ci_init(); ci_str_register(); }
static void teardown(void) { ci_shutdown(); }

int main(void) {
    setup();

    /* --- prepend to string with data → new data appears before existing --- */
    {
        ci_str *s = ci_str_from_cstr("world");
        assert(s != NULL);
        int ok = ci_str_prepend(s, "hello ", 6);
        assert(ok == 1);
        assert(ci_str_len(s) == 11);
        assert(memcmp(ci_str_head(s), "hello world", 11) == 0);
        ci_free(s);
    }

    /* --- prepend triggering ensure_head realloc: all data preserved --- */
    {
        ci_str *s = ci_str_from_cstr("suffix");
        assert(s != NULL);
        assert(ci_str_head_space(s) == 0); /* no headroom */

        int ok = ci_str_prepend(s, "prefix-", 7);
        assert(ok == 1);
        assert(ci_str_len(s) == 13);
        assert(memcmp(ci_str_head(s), "prefix-suffix", 13) == 0);
        ci_free(s);
    }

    /* --- multiple prepends: order correct (most recent at head) --- */
    {
        ci_str *s = ci_str_new(128);
        assert(s != NULL);
        ci_str_append(s, "C", 1);
        ci_str_prepend(s, "B", 1);
        ci_str_prepend(s, "A", 1);
        assert(ci_str_len(s) == 3);
        assert(memcmp(ci_str_head(s), "ABC", 3) == 0);
        ci_free(s);
    }

    /* --- prepend on ci_str_small → upgrades to full ci_str, prepend succeeds --- */
    {
        ci_str *ss = ci_str_small_new("tiny", 4);
        assert(ss != NULL);
        assert(CI_IS_STR_SMALL(ss));
        int ok = ci_str_prepend(ss, "x", 1);
        assert(ok == 1);
        assert(!CI_IS_STR_SMALL(ss)); /* upgraded */
        assert(ci_str_len(ss) == 5);
        assert(memcmp(ci_str_head(ss), "xtiny", 5) == 0);
        ci_free(ss);
    }

    /* --- prepend resets hash --- */
    {
        ci_str *s = ci_str_from_cstr("end");
        uint32_t h = ci_str_hash(s);
        assert(h != 0);
        ci_str_prepend(s, "front-", 6);
        assert(s->hash == 0);
        ci_free(s);
    }

    teardown();
    printf("test_prepend: PASSED\n");
    return 0;
}
