/* test_small_boundaries.c — pool selection edge cases */
#include "ciobj.c"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static void setup(void)    { ci_init(); ci_str_register(); }
static void teardown(void) { ci_shutdown(); }

int main(void) {
    setup();

    size_t hdr    = sizeof(ci_str_small);
    size_t cap64  = 64  - hdr;
    size_t cap128 = 128 - hdr;
    size_t cap256 = 256 - hdr;

    char d[256];
    memset(d, 'a', sizeof(d));

    /* --- exact-fit → CI_STR_SMALL_64 --- */
    {
        ci_str *s = ci_str_small_new(d, (uint8_t)cap64);
        assert(s != NULL);
        assert(tg_ptr_tag_full(s) == CI_STR_SMALL_64);
        assert(ci_str_size(s) == cap64);
        ci_free(s);
    }

    /* --- overflow 64: cap64+1 → CI_STR_SMALL_128 --- */
    {
        ci_str *s = ci_str_small_new(d, (uint8_t)(cap64 + 1));
        assert(s != NULL);
        assert(tg_ptr_tag_full(s) == CI_STR_SMALL_128);
        assert(ci_str_size(s) == cap128);
        assert(ci_str_len(s)  == cap64 + 1);
        ci_free(s);
    }

    /* --- exact-fit → CI_STR_SMALL_128 --- */
    {
        ci_str *s = ci_str_small_new(d, (uint8_t)cap128);
        assert(s != NULL);
        assert(tg_ptr_tag_full(s) == CI_STR_SMALL_128);
        assert(ci_str_size(s) == cap128);
        ci_free(s);
    }

    /* --- overflow 128: cap128+1 → CI_STR_SMALL_256 --- */
    {
        ci_str *s = ci_str_small_new(d, (uint8_t)(cap128 + 1));
        assert(s != NULL);
        assert(tg_ptr_tag_full(s) == CI_STR_SMALL_256);
        assert(ci_str_size(s) == cap256);
        assert(ci_str_len(s)  == cap128 + 1);
        ci_free(s);
    }

    /* --- exact-fit → CI_STR_SMALL_256 --- */
    {
        ci_str *s = ci_str_small_new(d, (uint8_t)cap256);
        assert(s != NULL);
        assert(tg_ptr_tag_full(s) == CI_STR_SMALL_256);
        assert(ci_str_size(s) == cap256);
        ci_free(s);
    }

    /* --- overflow 256: cap256+1 → NULL --- */
    {
        ci_str *s = ci_str_small_new(d, (uint8_t)(cap256 + 1));
        assert(s == NULL);
    }

    /* --- len=0 → smallest pool (CI_STR_SMALL_64) --- */
    {
        ci_str *s = ci_str_small_new("", 0);
        assert(s != NULL);
        assert(tg_ptr_tag_full(s) == CI_STR_SMALL_64);
        assert(ci_str_size(s) == cap64);
        ci_free(s);
    }

    teardown();
    printf("test_small_boundaries: PASSED\n");
    return 0;
}
