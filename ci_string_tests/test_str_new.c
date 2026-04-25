/* test_str_new.c — ci_str allocation and lifecycle */
#include "ciobj.c"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static void setup(void)    { ci_init(); ci_str_register(); }
static void teardown(void) { ci_shutdown(); }

int main(void) {
    setup();

    /* --- basic allocation --- */
    {
        ci_str *s = ci_str_new(64);
        assert(s != NULL);
        assert(CI_IS_ANY_STR(s));
        assert(CI_IS_STR(s));
        assert(!CI_IS_STR_SMALL(s));
        assert(ci_is_refcountable(s));
        assert(ci_str_len(s)        == 0);
        assert(ci_str_size(s)       >= 64);
        assert(ci_str_head_space(s) == 0);
        assert(ci_str_tail_space(s) >= 64);
        ci_free(s);
    }

    /* --- ci_str_new(0) works --- */
    {
        ci_str *s = ci_str_new(0);
        assert(s != NULL);
        assert(ci_str_len(s) == 0);
        assert(ci_str_size(s) >= 1); /* allocates at least 1 byte */
        ci_free(s);
    }

    /* --- refcount starts at 1 --- */
    {
        ci_str *s = ci_str_new(16);
        assert(s != NULL);
        assert(ci_refcnt(s) == 1);
        assert(ci_is_refcountable(s));

        ci_inc(s);
        assert(ci_refcnt(s) == 2);

        int freed = ci_dec(s);  /* refcnt → 1, not freed */
        assert(freed == 0);
        assert(ci_refcnt(s) == 1);

        freed = ci_dec(s);      /* refcnt → 0, freed */
        assert(freed == 1);
    }

    /* --- ci_free unconditional free --- */
    {
        ci_str *s = ci_str_new(32);
        assert(s != NULL);
        ci_free(s); /* must not crash */
    }

    teardown();
    printf("test_str_new: PASSED\n");
    return 0;
}
