/* test_clear.c — ci_str_clear / ci_str_clear_headroom */
#include "ciobj.c"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static void setup(void)    { ci_init(); ci_str_register(); }
static void teardown(void) { ci_shutdown(); }

int main(void) {
    setup();

    /* --- clear: len=0, start==memory, hash=0, size preserved --- */
    {
        ci_str *s = ci_str_from_cstr("hello world");
        assert(s != NULL);
        size_t size_before = ci_str_size(s);

        uint32_t h = ci_str_hash(s);
        assert(h != 0);

        ci_str_clear(s);

        assert(ci_str_len(s)        == 0);
        assert(ci_str_head_space(s) == 0);
        assert(s->start == s->memory);
        assert(s->hash  == 0);
        assert(ci_str_size(s) == size_before); /* buffer preserved */

        ci_free(s);
    }

    /* --- clear preserves buffer allocation --- */
    {
        ci_str *s = ci_str_new(1024);
        assert(s != NULL);
        ci_str_append(s, "data", 4);
        size_t sz = ci_str_size(s);
        uint8_t *mem = s->memory;

        ci_str_clear(s);

        assert(s->memory   == mem); /* same buffer */
        assert(ci_str_size(s) == sz);

        ci_free(s);
    }

    /* --- clear_headroom(10): len=0, head_space=10 --- */
    {
        ci_str *s = ci_str_new(64);
        assert(s != NULL);
        ci_str_append(s, "some data", 9);

        ci_str_clear_headroom(s, 10);

        assert(ci_str_len(s)        == 0);
        assert(ci_str_head_space(s) == 10);
        assert(s->start == s->memory + 10);
        assert(s->end   == s->memory + 10);
        assert(s->hash  == 0);

        ci_free(s);
    }

    /* --- clear_headroom(0) equivalent to clear --- */
    {
        ci_str *s = ci_str_from_cstr("test");
        ci_str_clear_headroom(s, 0);
        assert(ci_str_len(s)        == 0);
        assert(ci_str_head_space(s) == 0);
        assert(s->start == s->memory);
        ci_free(s);
    }

    /* --- clear_headroom(size): head_space matches requested --- */
    {
        ci_str *s = ci_str_new(32);
        assert(s != NULL);
        size_t sz = ci_str_size(s);

        ci_str_clear_headroom(s, sz);

        assert(ci_str_len(s)        == 0);
        assert(ci_str_head_space(s) >= sz);

        ci_free(s);
    }

    teardown();
    printf("test_clear: PASSED\n");
    return 0;
}
