/*
 * ci_string_test.c — test suite for ci_string
 *
 * Include chain:
 *   ci_string_test.c
 *     -> ciobj.c  (tgmemlib.c + ciobj.h + ci_string.c)
 */
#define CI_STRING_TEST
#include "ciobj.c"

#include <stdio.h>
#include <string.h>

static int g_fail;

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        g_fail++; \
    } \
} while (0)

/* ================================================================
 * Small strings
 * ================================================================ */

static void test_small(void)
{
    uint16_t hdr   = (uint16_t)sizeof(ci_str_small);
    uint8_t  cap32 = (uint8_t)(32  - hdr);
    uint8_t  cap64 = (uint8_t)(64  - hdr);
    uint8_t  cap256 = (uint8_t)(256 - hdr);

    char buf[256];
    memset(buf, 'a', sizeof(buf));

    /* empty */
    ci_str_small *s = ci_str_small_new("", 0);
    CHECK(s != NULL && s->length == 0);
    tg_free(s);

    /* single char */
    s = ci_str_small_new("Z", 1);
    CHECK(s != NULL);
    CHECK(s->length == 1 && s->data[0] == 'Z');
    CHECK(ci_str_small_cap(s) == cap32);
    tg_free(s);

    /* exact fit for 32-byte slot */
    s = ci_str_small_new(buf, cap32);
    CHECK(s != NULL && s->length == cap32);
    CHECK(ci_str_small_cap(s) == cap32);
    CHECK(memcmp(s->data, buf, cap32) == 0);
    tg_free(s);

    /* one byte over 32 → lands in 64-slot */
    s = ci_str_small_new(buf, (uint8_t)(cap32 + 1));
    CHECK(s != NULL && s->length == (uint8_t)(cap32 + 1));
    CHECK(ci_str_small_cap(s) == cap64);
    tg_free(s);

    /* exact fit for 64, 128, 256 */
    uint8_t cap128 = (uint8_t)(128 - hdr);
    for (int i = 0; i < 3; i++) {
        uint8_t cap = (uint8_t[]){ cap64, cap128, cap256 }[i];
        s = ci_str_small_new(buf, cap);
        CHECK(s != NULL && ci_str_small_cap(s) == cap);
        tg_free(s);
    }

    /* exceed 256-slot → NULL */
    if (cap256 < 255) {
        s = ci_str_small_new(buf, (uint8_t)(cap256 + 1));
        CHECK(s == NULL);
    }
}

/* ================================================================
 * ci_str lifecycle
 * ================================================================ */

static void test_lifecycle(void)
{
    /* new: verify all four pointers */
    ci_str *s = ci_str_new(256, 64);
    CHECK(s != NULL);
    CHECK(s->start == s->memory + 64);
    CHECK(s->end   == s->start);
    CHECK(s->limit == s->memory + 256);
    CHECK(s->hash  == 0);
    CHECK(ci_str_len(s)        == 0);
    CHECK(ci_str_head_space(s) == 64);
    CHECK(ci_str_tail_space(s) == 192);
    ci_str_free(s);

    /* headroom > cap → NULL */
    s = ci_str_new(10, 20);
    CHECK(s == NULL);

    /* from_cstr: content, length, no embedded null, no headroom */
    s = ci_str_from_cstr("hello", 0);
    CHECK(s != NULL && ci_str_len(s) == 5);
    CHECK(memcmp(s->start, "hello", 5) == 0);
    CHECK(s->start == s->memory);
    ci_str_free(s);

    /* from_cstr with headroom: start displaced */
    s = ci_str_from_cstr("world", 8);
    CHECK(s != NULL && ci_str_len(s) == 5);
    CHECK(s->start == s->memory + 8);
    ci_str_free(s);

    /* copy: deep, independent buffer */
    ci_str *orig = ci_str_from_cstr("abcde", 0);
    ci_str *copy = ci_str_copy(orig, 32);
    CHECK(copy != NULL && ci_str_len(copy) == 5);
    CHECK(memcmp(copy->start, "abcde", 5) == 0);
    CHECK(copy->memory != orig->memory);
    CHECK(ci_str_tail_space(copy) == 32);
    orig->start[0] = 'X';              /* mutate orig */
    CHECK(copy->start[0] == 'a');      /* copy unaffected */
    ci_str_free(orig);
    ci_str_free(copy);

    /* dec: refcnt 1 → 0, freed, returns 1 */
    s = ci_str_new(64, 0);
    CHECK(ci_str_dec(s) == 1);
    /* s freed; don't touch */

    /* dec: refcnt 2 → 1, not freed, returns 0 */
    s = ci_str_new(64, 0);
    s->gc.refcnt = 2;
    CHECK(ci_str_dec(s) == 0);
    CHECK(s->gc.refcnt == 1);
    ci_str_free(s);

    /* dec: saturated 0xFFFF → no-op, returns 0 */
    s = ci_str_new(64, 0);
    s->gc.refcnt = 0xFFFF;
    CHECK(ci_str_dec(s) == 0);
    CHECK(s->gc.refcnt == 0xFFFF);
    ci_str_free(s);
}

/* ================================================================
 * Buffer management
 * ================================================================ */

static void test_buffer_mgmt(void)
{
    /* ensure_tail: no realloc when space exists */
    ci_str *s = ci_str_new(128, 0);
    uint8_t *p = ci_str_ensure_tail(s, 64);
    CHECK(p == s->end);
    CHECK(s->limit == s->memory + 128);
    ci_str_free(s);

    /* ensure_tail: realloc, existing data and pointers preserved */
    s = ci_str_from_cstr("hello", 0);
    p = ci_str_ensure_tail(s, 4096);
    CHECK(p != NULL);
    CHECK(ci_str_len(s) == 5);
    CHECK(memcmp(s->start, "hello", 5) == 0);
    CHECK(ci_str_tail_space(s) >= 4096);
    ci_str_free(s);

    /* put_tail: advances end, resets hash */
    s = ci_str_new(64, 0);
    s->hash = 42;
    ci_str_ensure_tail(s, 8);
    ci_str_put_tail(s, 8);
    CHECK(ci_str_len(s) == 8);
    CHECK(s->hash == 0);
    ci_str_free(s);

    /* SKB zero-copy read pattern */
    s = ci_str_new(64, 0);
    uint8_t *tail = ci_str_ensure_tail(s, 5);
    CHECK(tail != NULL);
    memcpy(tail, "world", 5);
    ci_str_put_tail(s, 5);
    CHECK(ci_str_len(s) == 5);
    CHECK(memcmp(s->start, "world", 5) == 0);
    ci_str_free(s);

    /* ensure_head: retreat in-place when head space exists */
    s = ci_str_new(64, 16);
    ci_str_append(s, "data", 4);
    uint8_t *old_end = s->end;
    p = ci_str_ensure_head(s, 8);
    CHECK(p == s->start);
    CHECK(s->start == s->memory + 8);  /* retreated by 8 from 16 */
    CHECK(s->end   == old_end);
    CHECK(s->hash  == 0);
    ci_str_free(s);

    /* ensure_head: fresh buffer when insufficient, existing data preserved */
    s = ci_str_from_cstr("existing", 0);   /* no head space */
    uint8_t *old_mem = s->memory;
    p = ci_str_ensure_head(s, 32);
    CHECK(p != NULL);
    CHECK(s->memory != old_mem);
    CHECK(s->start  == p);
    /* existing data sits 32 bytes after new start */
    CHECK(memcmp(s->start + 32, "existing", 8) == 0);
    ci_str_free(s);

    /* ensure_head on a small string: NULL + stderr warning */
    ci_str_small *sm = ci_str_small_new("hi", 2);
    p = ci_str_ensure_head((ci_str *)sm, 4);
    CHECK(p == NULL);
    tg_free(sm);

    /* compact: memmove data to base */
    s = ci_str_new(64, 16);
    ci_str_append(s, "test", 4);
    CHECK(s->start != s->memory);
    ci_str_compact(s);
    CHECK(s->start == s->memory);
    CHECK(ci_str_len(s) == 4);
    CHECK(memcmp(s->start, "test", 4) == 0);
    ci_str_free(s);

    /* compact when already compact: no-op */
    s = ci_str_from_cstr("noop", 0);
    CHECK(s->start == s->memory);
    ci_str_compact(s);
    CHECK(s->start == s->memory && ci_str_len(s) == 4);
    ci_str_free(s);
}

/* ================================================================
 * Data operations
 * ================================================================ */

static void test_data_ops(void)
{
    /* append: basic */
    ci_str *s = ci_str_new(64, 0);
    CHECK(ci_str_append(s, "foo", 3) == 1);
    CHECK(ci_str_len(s) == 3);
    CHECK(memcmp(s->start, "foo", 3) == 0);

    /* append: accumulate */
    CHECK(ci_str_append(s, "bar", 3) == 1);
    CHECK(ci_str_len(s) == 6);
    CHECK(memcmp(s->start, "foobar", 6) == 0);
    ci_str_free(s);

    /* append: triggers realloc, existing data preserved */
    s = ci_str_from_cstr("head", 0);
    char big[4096];
    memset(big, 'x', sizeof(big));
    CHECK(ci_str_append(s, big, sizeof(big)) == 1);
    CHECK(ci_str_len(s) == 4 + sizeof(big));
    CHECK(memcmp(s->start, "head", 4) == 0);
    ci_str_free(s);

    /* prepend: with head space */
    s = ci_str_new(64, 16);
    ci_str_append(s, "world", 5);
    CHECK(ci_str_prepend(s, "hello ", 6) == 1);
    CHECK(ci_str_len(s) == 11);
    CHECK(memcmp(s->start, "hello world", 11) == 0);
    ci_str_free(s);

    /* prepend: triggers fresh buffer (no head space) */
    s = ci_str_from_cstr("world", 0);
    CHECK(ci_str_prepend(s, "hello ", 6) == 1);
    CHECK(ci_str_len(s) == 11);
    CHECK(memcmp(s->start, "hello world", 11) == 0);
    ci_str_free(s);

    /* rmhead: basic */
    s = ci_str_from_cstr("abcde", 0);
    CHECK(ci_str_rmhead(s, 2) == 2);
    CHECK(ci_str_len(s) == 3);
    CHECK(memcmp(s->start, "cde", 3) == 0);
    ci_str_free(s);

    /* rmhead: clamped to length */
    s = ci_str_from_cstr("abc", 0);
    CHECK(ci_str_rmhead(s, 100) == 3);
    CHECK(ci_str_len(s) == 0);
    ci_str_free(s);

    /* rmtail: basic */
    s = ci_str_from_cstr("abcde", 0);
    CHECK(ci_str_rmtail(s, 2) == 2);
    CHECK(ci_str_len(s) == 3);
    CHECK(memcmp(s->start, "abc", 3) == 0);
    ci_str_free(s);

    /* rmtail: clamped to length */
    s = ci_str_from_cstr("abc", 0);
    CHECK(ci_str_rmtail(s, 100) == 3);
    CHECK(ci_str_len(s) == 0);
    ci_str_free(s);

    /* clear: start = end = memory */
    s = ci_str_from_cstr("hello", 0);
    ci_str_clear(s);
    CHECK(ci_str_len(s) == 0);
    CHECK(s->start == s->memory);
    CHECK(s->end   == s->memory);
    ci_str_free(s);

    /* clear_headroom */
    s = ci_str_new(64, 0);
    ci_str_clear_headroom(s, 16);
    CHECK(ci_str_len(s) == 0);
    CHECK(s->start == s->memory + 16);
    CHECK(s->end   == s->memory + 16);
    ci_str_free(s);
}

/* ================================================================
 * Hash / compare
 * ================================================================ */

static void test_hash_cmp(void)
{
    /* empty string: FNV-1a offset basis (2166136261), nonzero, cached */
    ci_str *s = ci_str_new(16, 0);
    CHECK(s->hash == 0);
    uint32_t h = ci_str_hash(s);
    CHECK(h != 0);
    CHECK(s->hash == h);
    CHECK(ci_str_hash(s) == h);   /* idempotent */
    ci_str_free(s);

    /* two strings with same content → same hash */
    ci_str *a = ci_str_from_cstr("hello", 0);
    ci_str *b = ci_str_from_cstr("hello", 0);
    CHECK(ci_str_hash(a) == ci_str_hash(b));

    /* different content → almost certainly different hash */
    ci_str *c = ci_str_from_cstr("world", 0);
    CHECK(ci_str_hash(a) != ci_str_hash(c));
    ci_str_free(c);

    /* hash reset after append */
    ci_str_hash(a);
    CHECK(a->hash != 0);
    ci_str_append(a, "!", 1);
    CHECK(a->hash == 0);

    /* hash reset after rmhead */
    ci_str_hash(a);
    ci_str_rmhead(a, 1);
    CHECK(a->hash == 0);

    /* hash reset after rmtail */
    ci_str_hash(a);
    ci_str_rmtail(a, 1);
    CHECK(a->hash == 0);

    ci_str_free(a);

    /* eq: equal content */
    a = ci_str_from_cstr("test", 0);
    ci_str_clear(b);
    ci_str_append(b, "test", 4);
    CHECK(ci_str_eq(a, b) == 1);

    /* eq: same length, different content */
    ci_str_clear(b);
    ci_str_append(b, "TEST", 4);
    CHECK(ci_str_eq(a, b) == 0);

    /* eq: different lengths */
    ci_str_clear(b);
    ci_str_append(b, "te", 2);
    CHECK(ci_str_eq(a, b) == 0);

    ci_str_free(a);
    ci_str_free(b);

    /* eq_cstr: exact match */
    a = ci_str_from_cstr("hello", 0);
    CHECK(ci_str_eq_cstr(a, "hello")  == 1);
    CHECK(ci_str_eq_cstr(a, "hello!") == 0);   /* cstr longer */
    CHECK(ci_str_eq_cstr(a, "hell")   == 0);   /* cstr shorter */
    CHECK(ci_str_eq_cstr(a, "world")  == 0);   /* different */
    CHECK(ci_str_eq_cstr(a, "")       == 0);   /* empty */
    ci_str_free(a);

    /* eq_cstr: empty string matches empty cstr */
    a = ci_str_new(8, 0);
    CHECK(ci_str_eq_cstr(a, "") == 1);
    ci_str_free(a);
}

/* ================================================================
 * main
 * ================================================================ */

int main(void)
{
    ci_init();
    ci_str_register();

    test_small();
    test_lifecycle();
    test_buffer_mgmt();
    test_data_ops();
    test_hash_cmp();

    ci_shutdown();

    if (g_fail == 0)
        printf("ci_string: all tests passed\n");
    else
        printf("ci_string: %d test(s) FAILED\n", g_fail);
    return g_fail ? 1 : 0;
}
