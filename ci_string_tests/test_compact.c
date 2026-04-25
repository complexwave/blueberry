/* test_compact.c — eliminating head space */
#include "ciobj.c"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static void setup(void)    { ci_init(); ci_str_register(); }
static void teardown(void) { ci_shutdown(); }

int main(void) {
    setup();

    /* --- compact after ensure_head: head_space=0, data preserved --- */
    {
        ci_str *s = ci_str_from_cstr("hello world");
        assert(s != NULL);

        /* create head space */
        uint8_t *h = ci_str_ensure_head(s, 10);
        assert(h != NULL);
        assert(ci_str_head_space(s) > 0);

        size_t len_before = ci_str_len(s);
        char   buf[32];
        memcpy(buf, ci_str_head(s), len_before);

        ci_str_compact(s);

        assert(ci_str_head_space(s) == 0);
        assert(s->start == s->memory);
        assert(ci_str_len(s) == len_before);
        assert(memcmp(ci_str_head(s), buf, len_before) == 0);

        ci_free(s);
    }

    /* --- compact on already-compact string: no-op --- */
    {
        ci_str *s = ci_str_from_cstr("compact me");
        assert(s != NULL);
        assert(ci_str_head_space(s) == 0);
        assert(s->start == s->memory);

        uint8_t *mem_before   = s->memory;
        uint8_t *start_before = s->start;
        size_t   len_before   = ci_str_len(s);

        ci_str_compact(s); /* must be no-op */

        assert(s->memory == mem_before);
        assert(s->start  == start_before);
        assert(ci_str_len(s) == len_before);

        ci_free(s);
    }

    /* --- compact resets hash when data moves --- */
    {
        ci_str *s = ci_str_from_cstr("hash me");
        uint8_t *h = ci_str_ensure_head(s, 5);
        assert(h != NULL);
        assert(ci_str_head_space(s) > 0);

        uint32_t hash = ci_str_hash(s);
        assert(hash != 0);

        ci_str_compact(s);
        assert(s->hash == 0); /* hash reset when data moved */

        ci_free(s);
    }

    /* --- compact does NOT reset hash when already compact --- */
    {
        ci_str *s = ci_str_from_cstr("no move");
        assert(ci_str_head_space(s) == 0);

        uint32_t hash = ci_str_hash(s);
        assert(hash != 0);

        ci_str_compact(s); /* no-op: start == memory */
        assert(s->hash == hash); /* preserved */

        ci_free(s);
    }

    teardown();
    printf("test_compact: PASSED\n");
    return 0;
}
