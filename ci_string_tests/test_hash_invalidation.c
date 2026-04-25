/* test_hash_invalidation.c — hash reset on every mutating op */
#include "ciobj.c"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static void setup(void)    { ci_init(); ci_str_register(); }
static void teardown(void) { ci_shutdown(); }

/* Helper: build a fresh string with a cached hash */
static ci_str *fresh_hashed(const char *data) {
    ci_str *s = ci_str_from_cstr(data);
    assert(s != NULL);
    uint32_t h = ci_str_hash(s);
    assert(h != 0);
    return s;
}

int main(void) {
    setup();

    /* --- put_tail: resets hash (even for n=0) --- */
    {
        ci_str *s = fresh_hashed("abc");
        uint8_t *tail = ci_str_ensure_tail(s, 4);
        assert(tail != NULL);
        /* ensure_tail itself must NOT reset hash */
        assert(s->hash != 0);
        ci_str_put_tail(s, 0);
        assert(s->hash == 0);
        ci_free(s);
    }

    /* --- ensure_head: resets hash (when it retreats start) --- */
    {
        ci_str *s = fresh_hashed("hello");
        ci_str_ensure_head(s, 4);
        assert(s->hash == 0);
        ci_free(s);
    }

    /* --- compact: resets hash when data moves --- */
    {
        ci_str *s = fresh_hashed("compact");
        ci_str_ensure_head(s, 5);  /* create head space, also resets hash */
        /* re-hash after head space created */
        uint32_t h = ci_str_hash(s);
        assert(h != 0);
        ci_str_compact(s);          /* moves data → hash reset */
        assert(s->hash == 0);
        ci_free(s);
    }

    /* --- compact: does NOT reset hash when already compact --- */
    {
        ci_str *s = fresh_hashed("already compact");
        assert(ci_str_head_space(s) == 0);
        uint32_t h = s->hash;
        ci_str_compact(s); /* no-op */
        assert(s->hash == h); /* preserved */
        ci_free(s);
    }

    /* --- append: resets hash --- */
    {
        ci_str *s = fresh_hashed("base");
        ci_str_append(s, "x", 1);
        assert(s->hash == 0);
        ci_free(s);
    }

    /* --- prepend: resets hash --- */
    {
        ci_str *s = fresh_hashed("end");
        ci_str_prepend(s, "start-", 6);
        assert(s->hash == 0);
        ci_free(s);
    }

    /* --- rmhead: resets hash --- */
    {
        ci_str *s = fresh_hashed("remove-me");
        ci_str_rmhead(s, 3);
        assert(s->hash == 0);
        ci_free(s);
    }

    /* --- rmtail: resets hash --- */
    {
        ci_str *s = fresh_hashed("remove-me");
        ci_str_rmtail(s, 3);
        assert(s->hash == 0);
        ci_free(s);
    }

    /* --- clear: resets hash --- */
    {
        ci_str *s = fresh_hashed("clearme");
        ci_str_clear(s);
        assert(s->hash == 0);
        ci_free(s);
    }

    /* --- clear_headroom: resets hash --- */
    {
        ci_str *s = fresh_hashed("clearroom");
        ci_str_clear_headroom(s, 4);
        assert(s->hash == 0);
        ci_free(s);
    }

    /* --- ensure_tail: does NOT reset hash --- */
    {
        ci_str *s = fresh_hashed("preserve-hash");
        uint32_t h = s->hash;
        ci_str_ensure_tail(s, 1024); /* may realloc, must NOT reset hash */
        assert(s->hash == h);
        ci_free(s);
    }

    /* --- pure accessors: do NOT reset hash --- */
    {
        ci_str *s = fresh_hashed("read-only");
        uint32_t h = s->hash;

        (void)ci_str_len(s);
        (void)ci_str_size(s);
        (void)ci_str_head(s);
        (void)ci_str_tail(s);

        assert(s->hash == h);
        ci_free(s);
    }

    teardown();
    printf("test_hash_invalidation: PASSED\n");
    return 0;
}
