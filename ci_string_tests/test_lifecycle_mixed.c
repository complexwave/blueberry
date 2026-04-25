/* test_lifecycle_mixed.c — combined operation sequences */
#include "ciobj.c"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static void setup(void)    { ci_init(); ci_str_register(); }
static void teardown(void) { ci_shutdown(); }

int main(void) {
    setup();

    /* --- new → append → hash → append more → hash invalidated → rehash → eq --- */
    {
        ci_str *s = ci_str_new(32);
        assert(s != NULL);

        ci_str_append(s, "hello", 5);
        uint32_t h1 = ci_str_hash(s);
        assert(h1 != 0);

        ci_str_append(s, " world", 6);
        assert(s->hash == 0); /* hash invalidated by append */

        uint32_t h2 = ci_str_hash(s);
        assert(h2 != 0);
        assert(h2 != h1); /* different content → different hash */

        ci_str *copy = ci_str_from_cstr("hello world");
        assert(ci_str_eq(s, copy) == 1);

        ci_free(s);
        ci_free(copy);
    }

    /* --- new → prepend → rmhead → compact → verify data --- */
    {
        ci_str *s = ci_str_new(64);
        assert(s != NULL);

        ci_str_append(s, "base", 4);
        ci_str_prepend(s, "AAA-", 4);
        assert(ci_str_len(s) == 8);
        assert(memcmp(ci_str_head(s), "AAA-base", 8) == 0);

        ci_str_rmhead(s, 4); /* remove "AAA-" */
        assert(ci_str_len(s) == 4);
        assert(ci_str_head_space(s) == 4);
        assert(memcmp(ci_str_head(s), "base", 4) == 0);

        ci_str_compact(s);
        assert(ci_str_head_space(s) == 0);
        assert(ci_str_len(s) == 4);
        assert(memcmp(ci_str_head(s), "base", 4) == 0);

        ci_free(s);
    }

    /* --- from_cstr → copy → modify copy → original unchanged → eq returns 0 --- */
    {
        ci_str *orig = ci_str_from_cstr("original");
        ci_str *copy = ci_str_copy(orig, 16);
        assert(copy != NULL);

        /* Modify copy by appending */
        ci_str_append(copy, "-modified", 9);

        assert(ci_str_len(orig) == 8);
        assert(memcmp(ci_str_head(orig), "original", 8) == 0);
        assert(ci_str_eq(orig, copy) == 0);

        ci_free(orig);
        ci_free(copy);
    }

    /* --- Allocate 100 strings, free in various orders → no crash --- */
    {
        ci_str *pool[100];
        for (int i = 0; i < 100; i++) {
            char buf[16];
            snprintf(buf, sizeof(buf), "str%d", i);
            pool[i] = ci_str_from_cstr(buf);
            assert(pool[i] != NULL);
        }

        /* Free even indices first, then odd */
        for (int i = 0; i < 100; i += 2) ci_free(pool[i]);
        for (int i = 1; i < 100; i += 2) ci_free(pool[i]);
    }

    /* --- Small string and full string with same content: ci_str_len agrees via void* --- */
    {
        const char *data = "shared data";
        size_t      dlen = strlen(data);

        ci_str *fs = ci_str_from_cstr(data);
        ci_str *ss = ci_str_small_new(data, (uint8_t)dlen);
        assert(fs != NULL && ss != NULL);

        assert(ci_str_len((void *)fs) == dlen);
        assert(ci_str_len((void *)ss) == dlen);

        ci_free(fs);
        ci_free(ss);
    }

    /* --- Full lifecycle: new → append → prepend → rmhead → rmtail → hash → copy → eq → free --- */
    {
        ci_str *s = ci_str_new(32);
        assert(s != NULL);

        ci_str_append(s, "MIDDLE", 6);
        ci_str_prepend(s, "START-", 6);
        ci_str_append(s, "-END", 4);
        /* s = "START-MIDDLE-END" (16 bytes) */
        assert(ci_str_len(s) == 16);
        assert(memcmp(ci_str_head(s), "START-MIDDLE-END", 16) == 0);

        ci_str_rmhead(s, 6); /* remove "START-" */
        ci_str_rmtail(s, 4); /* remove "-END" */
        /* s = "MIDDLE" (6 bytes) */
        assert(ci_str_len(s) == 6);
        assert(memcmp(ci_str_head(s), "MIDDLE", 6) == 0);

        uint32_t h = ci_str_hash(s);
        assert(h != 0);

        ci_str *copy = ci_str_copy(s, 0);
        assert(copy != NULL);
        assert(ci_str_eq(s, copy) == 1);

        int freed = ci_dec(s);   /* refcnt 1→0 → freed */
        assert(freed == 1);
        ci_free(copy);
    }

    teardown();
    printf("test_lifecycle_mixed: PASSED\n");
    return 0;
}
