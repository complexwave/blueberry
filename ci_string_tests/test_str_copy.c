/* test_str_copy.c — ci_str_copy deep copy */
#include "ciobj.c"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static void setup(void)    { ci_init(); ci_str_register(); }
static void teardown(void) { ci_shutdown(); }

int main(void) {
    setup();

    /* --- copy with extra=0: same data, same len, no head space --- */
    {
        ci_str *orig = ci_str_from_cstr("hello world");
        assert(orig != NULL);

        ci_str *copy = ci_str_copy(orig, 0);
        assert(copy != NULL);
        assert(ci_str_len(copy) == ci_str_len(orig));
        assert(ci_str_head_space(copy) == 0);
        assert(memcmp(ci_str_head(copy), ci_str_head(orig), ci_str_len(orig)) == 0);

        ci_free(orig);
        ci_free(copy);
    }

    /* --- copy with extra=100: tail_space >= 100 --- */
    {
        ci_str *orig = ci_str_from_cstr("test");
        ci_str *copy = ci_str_copy(orig, 100);
        assert(copy != NULL);
        assert(ci_str_len(copy) == 4);
        assert(ci_str_tail_space(copy) >= 100);
        assert(memcmp(ci_str_head(copy), "test", 4) == 0);
        ci_free(orig);
        ci_free(copy);
    }

    /* --- modify copy → original unchanged (deep copy proof) --- */
    {
        ci_str *orig = ci_str_from_cstr("immutable");
        ci_str *copy = ci_str_copy(orig, 10);
        assert(copy != NULL);

        /* overwrite copy data */
        memset(ci_str_head(copy), 'X', ci_str_len(copy));

        /* original must be unaffected */
        assert(memcmp(ci_str_head(orig), "immutable", 9) == 0);
        ci_free(orig);
        ci_free(copy);
    }

    /* --- copy of string with head space → new string has NO head space --- */
    {
        ci_str *orig = ci_str_new(64);
        assert(orig != NULL);
        ci_str_append(orig, "data", 4);
        /* create head space by prepending */
        ci_str_prepend(orig, "pre:", 4);
        assert(ci_str_head_space(orig) > 0 || ci_str_len(orig) == 8); /* either way */

        /* now manually give it head space via ensure_head */
        ci_str *base = ci_str_from_cstr("payload");
        uint8_t *head = ci_str_ensure_head(base, 10);
        assert(head != NULL);
        assert(ci_str_head_space(base) > 0);

        ci_str *copy = ci_str_copy(base, 0);
        assert(copy != NULL);
        assert(ci_str_head_space(copy) == 0);
        assert(ci_str_len(copy) == ci_str_len(base));
        assert(memcmp(ci_str_head(copy), ci_str_head(base), ci_str_len(base)) == 0);

        ci_free(orig);
        ci_free(base);
        ci_free(copy);
    }

    /* --- refcount of copy is 1 (independent object) --- */
    {
        ci_str *orig = ci_str_from_cstr("shared?");
        ci_inc(orig); /* orig refcnt = 2 */
        ci_str *copy = ci_str_copy(orig, 0);
        assert(copy != NULL);
        assert(ci_refcnt(copy) == 1);
        assert(ci_refcnt(orig) == 2);
        ci_dec(orig); /* → 1 */
        ci_free(orig);
        ci_free(copy);
    }

    teardown();
    printf("test_str_copy: PASSED\n");
    return 0;
}
