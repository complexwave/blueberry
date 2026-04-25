/* test_syscall_pattern.c — simulated read(2)/write(2) I/O patterns */
#include "ciobj.c"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static void setup(void)    { ci_init(); ci_str_register(); }
static void teardown(void) { ci_shutdown(); }

int main(void) {
    setup();

    /* --- Scenario 1: Simulated read(2) --- */
    {
        ci_str *buf = ci_str_new(4096);
        assert(buf != NULL);

        /* Prepare "fake received" data */
        char fake[256];
        for (int i = 0; i < 256; i++) fake[i] = (char)('A' + i % 26);
        ssize_t n = 256;

        uint8_t *tail = ci_str_ensure_tail(buf, (size_t)n);
        assert(tail != NULL);
        memcpy(tail, fake, (size_t)n); /* simulate read() filling buffer */
        ci_str_put_tail(buf, (size_t)n);

        assert((ssize_t)ci_str_len(buf) == n);
        assert(memcmp(ci_str_head(buf), fake, (size_t)n) == 0);

        ci_free(buf);
    }

    /* --- Scenario 2: Simulated write(2) drain --- */
    {
        ci_str *buf = ci_str_new(4096);
        assert(buf != NULL);

        /* Put some data in */
        char data[512];
        for (int i = 0; i < 512; i++) data[i] = (char)('a' + i % 26);
        ci_str_append(buf, data, 512);
        assert(ci_str_len(buf) == 512);

        /* Simulate write(fd, head, 200) — "sent" 200 bytes */
        uint8_t *head = ci_str_head(buf);
        ssize_t sent = 200;
        assert(memcmp(head, data, (size_t)sent) == 0);
        ci_str_rmhead(buf, (size_t)sent);

        assert(ci_str_len(buf) == 312);
        assert(memcmp(ci_str_head(buf), data + 200, 312) == 0);

        ci_free(buf);
    }

    /* --- Scenario 3: Partial read + partial write loop --- */
    {
        /* Simulate socket relay: receive chunks into tail, send chunks from head */
        ci_str *buf = ci_str_new(1024);
        assert(buf != NULL);

        char source[4096];
        for (int i = 0; i < 4096; i++) source[i] = (char)('A' + i % 26);

        char sink[4096];
        memset(sink, 0, sizeof(sink));

        size_t total_received = 0;
        size_t total_sent     = 0;
        size_t recv_chunk = 137; /* odd size to stress alignment */
        size_t send_chunk = 89;

        while (total_sent < 4096) {
            /* Receive a chunk if source has more */
            if (total_received < 4096) {
                size_t to_recv = recv_chunk;
                if (total_received + to_recv > 4096) {
                    to_recv = 4096 - total_received;
                }
                uint8_t *tail = ci_str_ensure_tail(buf, to_recv);
                assert(tail != NULL);
                memcpy(tail, source + total_received, to_recv);
                ci_str_put_tail(buf, to_recv);
                total_received += to_recv;
            }

            /* Send a chunk from head */
            size_t avail = ci_str_len(buf);
            if (avail == 0) break;
            size_t to_send = send_chunk < avail ? send_chunk : avail;
            memcpy(sink + total_sent, ci_str_head(buf), to_send);
            ci_str_rmhead(buf, to_send);
            total_sent += to_send;
        }

        assert(total_sent == 4096);
        assert(memcmp(source, sink, 4096) == 0);

        ci_free(buf);
    }

    /* --- Scenario 4: Buffer wraps via drain-reset --- */
    {
        ci_str *buf = ci_str_new(256);
        assert(buf != NULL);
        size_t initial_size = ci_str_size(buf);

        char payload[128];
        memset(payload, 'P', sizeof(payload));

        /* Fill and drain completely */
        ci_str_append(buf, payload, 128);
        assert(ci_str_len(buf) == 128);

        ci_str_rmhead(buf, 128); /* full drain */
        assert(buf->start == buf->memory);
        assert(buf->end   == buf->memory);
        assert(ci_str_head_space(buf) == 0);

        /* Write again — should fit without realloc */
        uint8_t *mem_after = buf->memory;
        ci_str_append(buf, payload, 128);
        assert(ci_str_len(buf) == 128);
        assert(ci_str_size(buf) == initial_size); /* no realloc */
        assert(buf->memory == mem_after);

        ci_free(buf);
    }

    teardown();
    printf("test_syscall_pattern: PASSED\n");
    return 0;
}
