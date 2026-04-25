/* test_append.c — ci_str_append */
#include "ciobj.c"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static void setup(void)    { ci_init(); ci_str_register(); }
static void teardown(void) { ci_shutdown(); }

int main(void) {
    setup();

    /* --- append to empty string --- */
    {
        ci_str *s = ci_str_new(16);
        assert(s != NULL);
        int ok = ci_str_append(s, "hello", 5);
        assert(ok == 1);
        assert(ci_str_len(s) == 5);
        assert(memcmp(ci_str_head(s), "hello", 5) == 0);
        ci_free(s);
    }

    /* --- multiple appends: data concatenated in order --- */
    {
        ci_str *s = ci_str_new(32);
        assert(s != NULL);
        assert(ci_str_append(s, "foo", 3) == 1);
        assert(ci_str_append(s, "bar", 3) == 1);
        assert(ci_str_append(s, "baz", 3) == 1);
        assert(ci_str_len(s) == 9);
        assert(memcmp(ci_str_head(s), "foobarbaz", 9) == 0);
        ci_free(s);
    }

    /* --- append triggering realloc: data preserved --- */
    {
        ci_str *s = ci_str_new(4);
        assert(s != NULL);
        assert(ci_str_append(s, "ABCD", 4) == 1); /* fills buffer */
        assert(ci_str_append(s, "EFGH", 4) == 1); /* triggers realloc */
        assert(ci_str_len(s) == 8);
        assert(memcmp(ci_str_head(s), "ABCDEFGH", 8) == 0);
        ci_free(s);
    }

    /* --- append 0 bytes: success, no change --- */
    {
        ci_str *s = ci_str_from_cstr("static");
        size_t  old_len = ci_str_len(s);
        int ok = ci_str_append(s, "ignored", 0);
        assert(ok == 1);
        assert(ci_str_len(s) == old_len);
        ci_free(s);
    }

    /* --- append large data (> current buffer) --- */
    {
        char big[8192];
        for (int i = 0; i < 8192; i++) big[i] = (char)('A' + i % 26);
        ci_str *s = ci_str_new(16);
        assert(s != NULL);
        assert(ci_str_append(s, big, 8192) == 1);
        assert(ci_str_len(s) == 8192);
        assert(memcmp(ci_str_head(s), big, 8192) == 0);
        ci_free(s);
    }

    /* --- append resets hash --- */
    {
        ci_str *s = ci_str_from_cstr("abc");
        uint32_t h = ci_str_hash(s);
        assert(h != 0);
        ci_str_append(s, "d", 1);
        assert(s->hash == 0);
        ci_free(s);
    }

    teardown();
    printf("test_append: PASSED\n");
    return 0;
}
