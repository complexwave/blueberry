/* test_small_basic.c — ci_str_small fundamentals */
#include "ciobj.c"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static void setup(void)    { ci_init(); ci_str_register(); }
static void teardown(void) { ci_shutdown(); }

int main(void) {
    setup();

    /* Compute capacities dynamically — sizeof(ci_str_small) may include padding */
    size_t hdr    = sizeof(ci_str_small);
    size_t cap64  = 64  - hdr;
    size_t cap128 = 128 - hdr;
    size_t cap256 = 256 - hdr;

    /* --- 64-byte pool (smallest available; 32-byte reserved for internalized) --- */
    {
        char data[64];
        for (size_t i = 0; i < cap64; i++) data[i] = (char)('A' + i % 26);
        ci_str *s = ci_str_small_new(data, (uint8_t)cap64);
        assert(s != NULL);
        assert(CI_IS_ANY_STR(s));
        assert(CI_IS_STR_SMALL(s));
        assert(!CI_IS_STR(s));
        assert(!ci_is_refcountable(s));
        assert(ci_str_len(s)        == cap64);
        assert(ci_str_size(s)       == cap64);
        assert(ci_str_head_space(s) == 0);
        assert(ci_str_tail_space(s) == 0);
        assert(memcmp(ci_str_head(s), data, cap64) == 0);
        ci_str_reset_hash(s); /* no-op, must not crash */
        ci_free(s);
    }

    /* --- 128-byte pool --- */
    {
        char data[128];
        for (size_t i = 0; i < cap128; i++) data[i] = (char)('A' + i % 26);
        ci_str *s = ci_str_small_new(data, (uint8_t)cap128);
        assert(s != NULL);
        assert(CI_IS_STR_SMALL(s));
        assert(ci_str_len(s)  == cap128);
        assert(ci_str_size(s) == cap128);
        assert(memcmp(ci_str_head(s), data, cap128) == 0);
        ci_free(s);
    }

    /* --- 256-byte pool --- */
    {
        char data[256];
        for (size_t i = 0; i < cap256; i++) data[i] = (char)('A' + i % 26);
        ci_str *s = ci_str_small_new(data, (uint8_t)cap256);
        assert(s != NULL);
        assert(CI_IS_STR_SMALL(s));
        assert(ci_str_len(s)  == cap256);
        assert(ci_str_size(s) == cap256);
        assert(memcmp(ci_str_head(s), data, cap256) == 0);
        ci_free(s);
    }

    /* --- empty small string → smallest pool (64) --- */
    {
        ci_str *s = ci_str_small_new("", 0);
        assert(s != NULL);
        assert(ci_str_len(s)        == 0);
        assert(ci_str_size(s)       == cap64);
        assert(ci_str_head_space(s) == 0);
        assert(ci_str_tail_space(s) == cap64);
        assert(ci_str_tail(s) == ci_str_head(s));
        ci_free(s);
    }

    /* --- short string: partial fill → 64-byte pool --- */
    {
        ci_str *s = ci_str_small_new("hello", 5);
        assert(s != NULL);
        assert(ci_str_len(s)        == 5);
        assert(ci_str_size(s)       == cap64);
        assert(ci_str_head_space(s) == 0);
        assert(ci_str_tail_space(s) == cap64 - 5);
        assert(ci_str_tail(s) == ci_str_head(s) + 5);
        assert(memcmp(ci_str_head(s), "hello", 5) == 0);
        ci_free(s);
    }

    /* --- oversized → NULL --- */
    {
        char data[256];
        memset(data, 'X', sizeof(data));
        assert(ci_str_small_new(data, (uint8_t)(cap256 + 1)) == NULL);
    }

    teardown();
    printf("test_small_basic: PASSED\n");
    return 0;
}
