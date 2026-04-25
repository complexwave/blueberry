/* test_concat_big.c — large string concatenation stress */
#include "ciobj.c"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static void setup(void)    { ci_init(); ci_str_register(); }
static void teardown(void) { ci_shutdown(); }

int main(void) {
    setup();

    /* --- Append 1000 small chunks (13 bytes each) → verify total and data --- */
    {
        char chunk[13];
        for (int i = 0; i < 13; i++) chunk[i] = (char)('A' + i % 26);

        ci_str *s = ci_str_new(64);
        assert(s != NULL);

        for (int i = 0; i < 1000; i++) {
            int ok = ci_str_append(s, chunk, 13);
            assert(ok == 1);
        }

        assert(ci_str_len(s) == 13000);

        /* Verify each chunk */
        const uint8_t *p = ci_str_head(s);
        for (int i = 0; i < 1000; i++) {
            assert(memcmp(p + i * 13, chunk, 13) == 0);
        }

        ci_free(s);
    }

    /* --- Prepend 100 chunks → verify reverse order --- */
    {
        char chunks[100][4];
        for (int i = 0; i < 100; i++) {
            chunks[i][0] = (char)('A' + i / 26);
            chunks[i][1] = (char)('a' + i % 26);
            chunks[i][2] = '-';
            chunks[i][3] = (char)('0' + i % 10);
        }

        ci_str *s = ci_str_new(64);
        assert(s != NULL);

        /* Prepend in order 0..99: last prepended (99) ends up at head */
        for (int i = 0; i < 100; i++) {
            int ok = ci_str_prepend(s, chunks[i], 4);
            assert(ok == 1);
        }

        assert(ci_str_len(s) == 400);

        /* Head should be chunk[99], then chunk[98], ... chunk[0] */
        const uint8_t *p = ci_str_head(s);
        for (int i = 99; i >= 0; i--) {
            assert(memcmp(p + (99 - i) * 4, chunks[i], 4) == 0);
        }

        ci_free(s);
    }

    /* --- Alternate appends and prepends → combined result --- */
    {
        ci_str *s = ci_str_new(32);
        assert(s != NULL);

        /* Build "ABCDE" from middle out */
        ci_str_append(s,  "C", 1);
        ci_str_append(s,  "D", 1);
        ci_str_prepend(s, "B", 1);
        ci_str_append(s,  "E", 1);
        ci_str_prepend(s, "A", 1);

        assert(ci_str_len(s) == 5);
        assert(memcmp(ci_str_head(s), "ABCDE", 5) == 0);

        ci_free(s);
    }

    /* --- Append a single 1 MB block: data integrity --- */
    {
        size_t MB = 1024 * 1024;
        char *big = malloc(MB);
        assert(big != NULL);
        for (size_t i = 0; i < MB; i++) big[i] = (char)('A' + i % 26);

        ci_str *s = ci_str_new(16);
        assert(s != NULL);
        int ok = ci_str_append(s, big, MB);
        assert(ok == 1);
        assert(ci_str_len(s) == MB);
        assert(memcmp(ci_str_head(s), big, MB) == 0);

        free(big);
        ci_free(s);
    }

    /* --- Build to 10 MB via repeated 4KB appends --- */
    {
        size_t chunk_size = 4096;
        size_t target     = 10 * 1024 * 1024;
        size_t iterations = target / chunk_size;

        char *chunk = malloc(chunk_size);
        assert(chunk != NULL);
        memset(chunk, 0xAB, chunk_size);

        ci_str *s = ci_str_new(chunk_size);
        assert(s != NULL);

        for (size_t i = 0; i < iterations; i++) {
            int ok = ci_str_append(s, chunk, chunk_size);
            assert(ok == 1);
        }

        assert(ci_str_len(s) == target);

        free(chunk);
        ci_free(s);
    }

    teardown();
    printf("test_concat_big: PASSED\n");
    return 0;
}
