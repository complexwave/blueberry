/* test_str_from_cstr.c — ci_str_from_cstr */
#include "ciobj.c"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static void setup(void)    { ci_init(); ci_str_register(); }
static void teardown(void) { ci_shutdown(); }

int main(void) {
    setup();

    /* --- "hello" → len=5, data matches, eq_cstr true --- */
    {
        ci_str *s = ci_str_from_cstr("hello");
        assert(s != NULL);
        assert(ci_str_len(s) == 5);
        assert(memcmp(ci_str_head(s), "hello", 5) == 0);
        assert(ci_str_eq_cstr(s, "hello"));
        assert(ci_str_head_space(s) == 0);
        ci_free(s);
    }

    /* --- empty string → len=0 --- */
    {
        ci_str *s = ci_str_from_cstr("");
        assert(s != NULL);
        assert(ci_str_len(s) == 0);
        assert(ci_str_eq_cstr(s, ""));
        ci_free(s);
    }

    /* --- long string (1000 chars) data integrity --- */
    {
        char src[1001];
        for (int i = 0; i < 1000; i++) src[i] = (char)('A' + i % 26);
        src[1000] = '\0';

        ci_str *s = ci_str_from_cstr(src);
        assert(s != NULL);
        assert(ci_str_len(s) == 1000);
        assert(memcmp(ci_str_head(s), src, 1000) == 0);
        assert(ci_str_head_space(s) == 0);
        ci_free(s);
    }

    teardown();
    printf("test_str_from_cstr: PASSED\n");
    return 0;
}
