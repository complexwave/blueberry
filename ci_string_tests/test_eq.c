/* test_eq.c — ci_str_eq / ci_str_eq_cstr corner cases */
#include "ciobj.c"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static void setup(void)    { ci_init(); ci_str_register(); }
static void teardown(void) { ci_shutdown(); }

int main(void) {
    setup();

    /* --- equal strings → 1 --- */
    {
        ci_str *a = ci_str_from_cstr("hello");
        ci_str *b = ci_str_from_cstr("hello");
        assert(ci_str_eq(a, b) == 1);
        ci_free(a); ci_free(b);
    }

    /* --- different lengths → 0 --- */
    {
        ci_str *a = ci_str_from_cstr("hi");
        ci_str *b = ci_str_from_cstr("hello");
        assert(ci_str_eq(a, b) == 0);
        ci_free(a); ci_free(b);
    }

    /* --- same length, different content → 0 --- */
    {
        ci_str *a = ci_str_from_cstr("abcde");
        ci_str *b = ci_str_from_cstr("abcdf");
        assert(ci_str_eq(a, b) == 0);
        ci_free(a); ci_free(b);
    }

    /* --- both empty → 1 --- */
    {
        ci_str *a = ci_str_new(4);
        ci_str *b = ci_str_new(8);
        assert(ci_str_eq(a, b) == 1);
        ci_free(a); ci_free(b);
    }

    /* --- eq_cstr match → 1 --- */
    {
        ci_str *s = ci_str_from_cstr("world");
        assert(ci_str_eq_cstr(s, "world") == 1);
        ci_free(s);
    }

    /* --- eq_cstr: cstr longer → 0 --- */
    {
        ci_str *s = ci_str_from_cstr("hi");
        assert(ci_str_eq_cstr(s, "hi there") == 0);
        ci_free(s);
    }

    /* --- eq_cstr: cstr shorter → 0 --- */
    {
        ci_str *s = ci_str_from_cstr("hello");
        assert(ci_str_eq_cstr(s, "hell") == 0);
        ci_free(s);
    }

    /* --- eq_cstr: empty string vs "" → 1 --- */
    {
        ci_str *s = ci_str_new(8);
        assert(ci_str_eq_cstr(s, "") == 1);
        ci_free(s);
    }

    /* --- eq_cstr: empty string vs "x" → 0 --- */
    {
        ci_str *s = ci_str_new(8);
        assert(ci_str_eq_cstr(s, "x") == 0);
        ci_free(s);
    }

    /* --- string with head space: eq works on [start, end) --- */
    {
        ci_str *a = ci_str_from_cstr("data");
        ci_str *b = ci_str_new(32);
        ci_str_append(b, "xxdata", 6);
        ci_str_rmhead(b, 2); /* b now shows "data" with 2 bytes head space */
        assert(ci_str_head_space(b) == 2);
        assert(ci_str_len(b) == 4);
        assert(ci_str_eq(a, b) == 1);
        ci_free(a); ci_free(b);
    }

    /* --- binary data with embedded \0:
     *     ci_str_eq uses memcmp (sees full length),
     *     eq_cstr uses strncmp (stops at \0) — they can diverge --- */
    {
        /* Build two ci_str with same len but different post-\0 content */
        ci_str *a = ci_str_new(8);
        ci_str *b = ci_str_new(8);
        ci_str_append(a, "ab\0cd", 5);
        ci_str_append(b, "ab\0XY", 5);

        /* eq: sees the full 5 bytes — "cd" != "XY" */
        assert(ci_str_eq(a, b) == 0);

        /* eq_cstr: compares against "ab\0cd".
         * strncmp("ab\0cd", "ab\0cd", 5) — this compares len=5 bytes via strncmp,
         * but strncmp stops at the embedded \0, so it considers them equal if
         * the cstr argument also has \0 at position 2. Then cstr[5] must be '\0'. */
        /* Actually, eq_cstr: strncmp(s->start, cstr, len) then checks cstr[len]=='\0'.
         * strncmp stops at embedded \0, so strncmp("ab\0cd","ab\0cd",5)==0 because it
         * only compares up to first \0.
         * But strncmp("ab\0cd","ab\0XY",5) also == 0 for the same reason!
         * So eq_cstr("ab\0cd", "ab\0XY") would return 1 (false positive with binary data). */
        /* This demonstrates the divergence: eq_cstr is NOT safe for binary data. */
        const char cstr_with_null[6] = "ab\0XY"; /* 5 chars + nul term at [5] */
        /* eq returns 0 (correct binary comparison) */
        assert(ci_str_eq(a, b) == 0);
        /* eq_cstr on 'a' vs the XY version: strncmp stops at \0, sees "ab" == "ab" → 1 */
        assert(ci_str_eq_cstr(a, cstr_with_null) == 1);

        ci_free(a); ci_free(b);
    }

    teardown();
    printf("test_eq: PASSED\n");
    return 0;
}
