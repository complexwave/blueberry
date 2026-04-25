/* test_producer_consumer.c — dual-callback buffer sharing pattern */
#include "ciobj.c"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static void setup(void)    { ci_init(); ci_str_register(); }
static void teardown(void) { ci_shutdown(); }

int main(void) {
    setup();

    /* === Primary test: 1000 produce/consume cycles without realloc === */
    {
        ci_str *buf = ci_str_new(256);
        assert(buf != NULL);

        size_t initial_size = ci_str_size(buf);

        char chunk[64];
        memset(chunk, 'X', sizeof(chunk));

        for (int i = 0; i < 1000; i++) {
            /* Producer: write 64 bytes to tail */
            int ok = ci_str_append(buf, chunk, 64);
            assert(ok == 1);

            /* Consumer: read ALL data from head */
            size_t len = ci_str_len(buf);
            ci_str_rmhead(buf, len);

            /* Invariants: buffer fully drained, pointers reset to base */
            assert(ci_str_len(buf) == 0);
            assert(buf->start == buf->memory);
            assert(buf->end   == buf->memory);
            assert(ci_str_head_space(buf) == 0);
        }

        /* No realloc should have occurred */
        assert(ci_str_size(buf) == initial_size);

        ci_free(buf);
    }

    /* === Variant: partial consumption === */
    {
        ci_str *buf = ci_str_new(256);
        assert(buf != NULL);

        char payload[100];
        memset(payload, 'P', sizeof(payload));

        /* Producer writes 100 bytes */
        ci_str_append(buf, payload, 100);
        assert(ci_str_len(buf) == 100);

        /* Consumer reads 60 bytes (partial) */
        ci_str_rmhead(buf, 60);
        assert(ci_str_len(buf)        == 40);
        assert(ci_str_head_space(buf) == 60);      /* drifted, NOT reset */
        assert(buf->start != buf->memory);          /* not reset yet */

        /* Consumer reads remaining 40 bytes */
        ci_str_rmhead(buf, 40);
        assert(ci_str_len(buf) == 0);
        assert(buf->start == buf->memory);          /* drain-reset triggered */
        assert(buf->end   == buf->memory);
        assert(ci_str_head_space(buf) == 0);

        ci_free(buf);
    }

    /* === Variant: alternating 1-byte writes and full drains (10000 iterations) === */
    {
        ci_str *buf = ci_str_new(64);
        assert(buf != NULL);

        size_t initial_size = ci_str_size(buf);
        char c = 'Z';

        for (int i = 0; i < 10000; i++) {
            ci_str_append(buf, &c, 1);
            assert(ci_str_len(buf) == 1);

            ci_str_rmhead(buf, 1);
            assert(ci_str_len(buf) == 0);
            assert(buf->start == buf->memory);
            assert(buf->end   == buf->memory);
        }

        assert(ci_str_size(buf) == initial_size); /* no realloc */

        ci_free(buf);
    }

    teardown();
    printf("test_producer_consumer: PASSED\n");
    return 0;
}
