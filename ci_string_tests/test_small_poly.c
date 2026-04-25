/* test_small_poly.c — small string polymorphic behavior */
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

    /* ================================================================
     * Section 1 — ensure_tail on small string
     * ================================================================ */

    /* 1a: sufficient tail space → returns correct pointer */
    {
        ci_str *s = ci_str_small_new("hi", 2);
        uint8_t *p = ci_str_ensure_tail(s, 1);
        assert(p != NULL);
        assert(p == ci_str_tail(s));
        assert(ci_str_len(s) == 2);
        ci_free(s);
    }

    /* 1b: exceeds slot capacity → returns NULL */
    {
        ci_str *s = ci_str_small_new("", 0);
        uint8_t *p = ci_str_ensure_tail(s, cap64 + 1);
        assert(p == NULL);
        assert(ci_str_len(s) == 0);
        ci_free(s);
    }

    /* 1c: exactly at capacity → returns pointer */
    {
        ci_str *s = ci_str_small_new("", 0);
        uint8_t *p = ci_str_ensure_tail(s, cap64);
        assert(p != NULL);
        ci_free(s);
    }

    printf("  section 1 (ensure_tail): ok\n");

    /* ================================================================
     * Section 2 — put_tail on small string
     * ================================================================ */

    /* 2a: write bytes then commit */
    {
        ci_str *s = ci_str_small_new("AB", 2);
        uint8_t *p = ci_str_ensure_tail(s, 3);
        assert(p != NULL);
        memcpy(p, "XYZ", 3);
        ci_str_put_tail(s, 3);
        assert(ci_str_len(s) == 5);
        assert(memcmp(ci_str_head(s), "ABXYZ", 5) == 0);
        ci_free(s);
    }

    /* 2b: put_tail(0) — no change */
    {
        ci_str *s = ci_str_small_new("hello", 5);
        ci_str_put_tail(s, 0);
        assert(ci_str_len(s) == 5);
        ci_free(s);
    }

    printf("  section 2 (put_tail): ok\n");

    /* ================================================================
     * Section 3 — compact on small string (no-op)
     * ================================================================ */

    /* 3a: no-op, data unchanged */
    {
        ci_str *s = ci_str_small_new("hello", 5);
        ci_str_compact(s);
        assert(ci_str_len(s) == 5);
        assert(memcmp(ci_str_head(s), "hello", 5) == 0);
        ci_free(s);
    }

    printf("  section 3 (compact): ok\n");

    /* ================================================================
     * Section 4 — append on small string
     * ================================================================ */

    /* 4a: fits → success */
    {
        ci_str *s = ci_str_small_new("AB", 2);
        int r = ci_str_append(s, "CD", 2);
        assert(r == 1);
        assert(ci_str_len(s) == 4);
        assert(memcmp(ci_str_head(s), "ABCD", 4) == 0);
        ci_free(s);
    }

    /* 4b: append to empty */
    {
        ci_str *s = ci_str_small_new("", 0);
        int r = ci_str_append(s, "hello", 5);
        assert(r == 1);
        assert(ci_str_len(s) == 5);
        ci_free(s);
    }

    /* 4c: append 0 bytes */
    {
        ci_str *s = ci_str_small_new("hi", 2);
        int r = ci_str_append(s, "X", 0);
        assert(r == 1);
        assert(ci_str_len(s) == 2);
        ci_free(s);
    }

    /* 4d: overflow → returns 0, unchanged */
    {
        ci_str *s = ci_str_small_new("", 0);
        char buf[64];
        memset(buf, 'X', sizeof(buf));
        int r = ci_str_append(s, buf, cap64 + 1);
        assert(r == 0);
        assert(ci_str_len(s) == 0);
        ci_free(s);
    }

    /* 4e: repeated appends filling slot exactly */
    {
        ci_str *s = ci_str_small_new("", 0);
        char byte = 'A';
        for (size_t i = 0; i < cap64; i++) {
            int r = ci_str_append(s, &byte, 1);
            assert(r == 1);
        }
        assert(ci_str_len(s) == cap64);
        int r = ci_str_append(s, &byte, 1);
        assert(r == 0);
        assert(ci_str_len(s) == cap64);
        ci_free(s);
    }

    printf("  section 4 (append): ok\n");

    /* ================================================================
     * Section 5 — rmtail on small string
     * ================================================================ */

    /* 5a: basic rmtail */
    {
        ci_str *s = ci_str_small_new("hello", 5);
        size_t n = ci_str_rmtail(s, 3);
        assert(n == 3);
        assert(ci_str_len(s) == 2);
        assert(memcmp(ci_str_head(s), "he", 2) == 0);
        ci_free(s);
    }

    /* 5b: rmtail(0) */
    {
        ci_str *s = ci_str_small_new("hello", 5);
        size_t n = ci_str_rmtail(s, 0);
        assert(n == 0);
        assert(ci_str_len(s) == 5);
        ci_free(s);
    }

    /* 5c: clamped */
    {
        ci_str *s = ci_str_small_new("hi", 2);
        size_t n = ci_str_rmtail(s, 100);
        assert(n == 2);
        assert(ci_str_len(s) == 0);
        ci_free(s);
    }

    /* 5d: drain all then reuse */
    {
        ci_str *s = ci_str_small_new("hello", 5);
        ci_str_rmtail(s, 5);
        assert(ci_str_len(s) == 0);
        int r = ci_str_append(s, "X", 1);
        assert(r == 1);
        assert(ci_str_len(s) == 1);
        ci_free(s);
    }

    printf("  section 5 (rmtail): ok\n");

    /* ================================================================
     * Section 6 — rmhead on small string (memmove)
     * ================================================================ */

    /* 6a: basic rmhead — data shifts left */
    {
        ci_str *s = ci_str_small_new("hello", 5);
        size_t n = ci_str_rmhead(s, 2);
        assert(n == 2);
        assert(ci_str_len(s) == 3);
        assert(memcmp(ci_str_head(s), "llo", 3) == 0);
        ci_free(s);
    }

    /* 6b: rmhead(0) */
    {
        ci_str *s = ci_str_small_new("hello", 5);
        size_t n = ci_str_rmhead(s, 0);
        assert(n == 0);
        assert(ci_str_len(s) == 5);
        ci_free(s);
    }

    /* 6c: clamped */
    {
        ci_str *s = ci_str_small_new("hi", 2);
        size_t n = ci_str_rmhead(s, 100);
        assert(n == 2);
        assert(ci_str_len(s) == 0);
        ci_free(s);
    }

    /* 6d: drain all then reuse */
    {
        ci_str *s = ci_str_small_new("hello", 5);
        ci_str_rmhead(s, 5);
        assert(ci_str_len(s) == 0);
        int r = ci_str_append(s, "XY", 2);
        assert(r == 1);
        assert(memcmp(ci_str_head(s), "XY", 2) == 0);
        ci_free(s);
    }

    /* 6e: partial rmhead twice */
    {
        ci_str *s = ci_str_small_new("ABCDE", 5);
        ci_str_rmhead(s, 2);
        assert(ci_str_len(s) == 3);
        assert(memcmp(ci_str_head(s), "CDE", 3) == 0);
        ci_str_rmhead(s, 3);
        assert(ci_str_len(s) == 0);
        ci_free(s);
    }

    /* 6f: head_space always 0 after rmhead */
    {
        ci_str *s = ci_str_small_new("hello", 5);
        ci_str_rmhead(s, 3);
        assert(ci_str_head_space(s) == 0);
        ci_free(s);
    }

    printf("  section 6 (rmhead): ok\n");

    /* ================================================================
     * Section 7 — clear on small string
     * ================================================================ */

    /* 7a: sets length to 0, null-terminates */
    {
        ci_str *s = ci_str_small_new("hello", 5);
        ci_str_clear(s);
        assert(ci_str_len(s) == 0);
        assert(ci_str_tail_space(s) == ci_str_size(s));
        assert(ci_str_head(s)[0] == '\0');
        ci_free(s);
    }

    /* 7b: clear then reuse */
    {
        ci_str *s = ci_str_small_new("hello", 5);
        ci_str_clear(s);
        int r = ci_str_append(s, "new", 3);
        assert(r == 1);
        assert(ci_str_len(s) == 3);
        assert(memcmp(ci_str_head(s), "new", 3) == 0);
        ci_free(s);
    }

    printf("  section 7 (clear): ok\n");

    /* ================================================================
     * Section 8 — clear_headroom on small string (upgrade)
     * ================================================================ */

    /* 8a: clear_headroom triggers upgrade */
    {
        ci_str *s = ci_str_small_new("hello", 5);
        ci_str_clear_headroom(s, 10);
        assert(!CI_IS_STR_SMALL(s));
        assert(ci_str_len(s) == 0);
        assert(ci_str_head_space(s) == 10);
        ci_free(s);
    }

    /* 8b: clear_headroom(0) — equivalent to clear, small stays small */
    {
        ci_str *s = ci_str_small_new("hello", 5);
        ci_str_clear_headroom(s, 0);
        assert(ci_str_len(s) == 0);
        ci_free(s);
    }

    printf("  section 8 (clear_headroom): ok\n");

    /* ================================================================
     * Section 9 — hash on small string
     * ================================================================ */

    /* 9a: nonzero for non-empty */
    {
        ci_str *s = ci_str_small_new("hello", 5);
        uint32_t h = ci_str_hash(s);
        assert(h != 0);
        ci_free(s);
    }

    /* 9b: same content → same hash */
    {
        ci_str *a = ci_str_small_new("hello", 5);
        ci_str *b = ci_str_small_new("hello", 5);
        assert(ci_str_hash(a) == ci_str_hash(b));
        ci_free(a);
        ci_free(b);
    }

    /* 9c: different content → different hash */
    {
        ci_str *a = ci_str_small_new("hello", 5);
        ci_str *b = ci_str_small_new("world", 5);
        assert(ci_str_hash(a) != ci_str_hash(b));
        ci_free(a);
        ci_free(b);
    }

    /* 9d: empty small string hash — nonzero (sentinel) */
    {
        ci_str *s = ci_str_small_new("", 0);
        uint32_t h = ci_str_hash(s);
        assert(h != 0);
        assert(h == ci_str_hash(s));
        ci_free(s);
    }

    /* 9e: matches equivalent full ci_str */
    {
        ci_str *s  = ci_str_small_new("hello", 5);
        ci_str *fs = ci_str_from_cstr("hello");
        assert(ci_str_hash(s) == ci_str_hash(fs));
        ci_free(s);
        ci_dec(fs);
    }

    printf("  section 9 (hash): ok\n");

    /* ================================================================
     * Section 10 — eq / eq_cstr on small strings
     * ================================================================ */

    /* 10a: small == small, same content → 1 */
    {
        ci_str *a = ci_str_small_new("hello", 5);
        ci_str *b = ci_str_small_new("hello", 5);
        assert(ci_str_eq(a, b) == 1);
        ci_free(a);
        ci_free(b);
    }

    /* 10b: small == small, different → 0 */
    {
        ci_str *a = ci_str_small_new("hello", 5);
        ci_str *b = ci_str_small_new("world", 5);
        assert(ci_str_eq(a, b) == 0);
        ci_free(a);
        ci_free(b);
    }

    /* 10c: small == full, same content → 1 (both directions) */
    {
        ci_str *s  = ci_str_small_new("hello", 5);
        ci_str *fs = ci_str_from_cstr("hello");
        assert(ci_str_eq(s, fs) == 1);
        assert(ci_str_eq(fs, s) == 1);
        ci_free(s);
        ci_dec(fs);
    }

    /* 10d: small == full, different → 0 */
    {
        ci_str *s  = ci_str_small_new("hello", 5);
        ci_str *fs = ci_str_from_cstr("world");
        assert(ci_str_eq(s, fs) == 0);
        ci_free(s);
        ci_dec(fs);
    }

    /* 10e: different lengths → 0 */
    {
        ci_str *a = ci_str_small_new("hi", 2);
        ci_str *b = ci_str_small_new("hello", 5);
        assert(ci_str_eq(a, b) == 0);
        ci_free(a);
        ci_free(b);
    }

    /* 10f: both empty → 1 */
    {
        ci_str *a = ci_str_small_new("", 0);
        ci_str *b = ci_str_small_new("", 0);
        assert(ci_str_eq(a, b) == 1);
        ci_free(a);
        ci_free(b);
    }

    /* 10g: eq_cstr match */
    {
        ci_str *s = ci_str_small_new("hello", 5);
        assert(ci_str_eq_cstr(s, "hello") == 1);
        ci_free(s);
    }

    /* 10h: eq_cstr cstr longer → 0 */
    {
        ci_str *s = ci_str_small_new("hell", 4);
        assert(ci_str_eq_cstr(s, "hello") == 0);
        ci_free(s);
    }

    /* 10i: eq_cstr cstr shorter → 0 */
    {
        ci_str *s = ci_str_small_new("hello", 5);
        assert(ci_str_eq_cstr(s, "hell") == 0);
        ci_free(s);
    }

    /* 10j: eq_cstr empty vs "" → 1 */
    {
        ci_str *s = ci_str_small_new("", 0);
        assert(ci_str_eq_cstr(s, "") == 1);
        ci_free(s);
    }

    printf("  section 10 (eq/eq_cstr): ok\n");

    /* ================================================================
     * Section 11 — copy from small string source
     * ================================================================ */

    /* 11a: copy → new independent full ci_str */
    {
        ci_str *s    = ci_str_small_new("hello", 5);
        ci_str *copy = ci_str_copy(s, 0);
        assert(CI_IS_STR(copy));
        assert(ci_str_len(copy) == 5);
        assert(memcmp(ci_str_head(copy), "hello", 5) == 0);
        assert(ci_str_eq(s, copy) == 1);
        ci_free(s);
        ci_dec(copy);
    }

    /* 11b: copy with extra tail */
    {
        ci_str *s    = ci_str_small_new("hi", 2);
        ci_str *copy = ci_str_copy(s, 50);
        assert(ci_str_len(copy) == 2);
        assert(ci_str_tail_space(copy) >= 50);
        ci_free(s);
        ci_dec(copy);
    }

    /* 11c: copy of empty small */
    {
        ci_str *s    = ci_str_small_new("", 0);
        ci_str *copy = ci_str_copy(s, 0);
        assert(ci_str_len(copy) == 0);
        ci_free(s);
        ci_dec(copy);
    }

    printf("  section 11 (copy): ok\n");

    /* ================================================================
     * Section 12 — ensure_head / prepend upgrade
     * ================================================================ */

    /* 12a: ensure_head upgrades small to full */
    {
        ci_str *s = ci_str_small_new("hello", 5);
        assert(CI_IS_STR_SMALL(s));
        uint8_t *p = ci_str_ensure_head(s, 10);
        assert(p != NULL);
        assert(!CI_IS_STR_SMALL(s));
        assert(ci_str_len(s) == 5);
        assert(memcmp(ci_str_head(s), "hello", 5) == 0);
        assert(ci_str_head_space(s) >= 10);
        ci_free(s);
    }

    /* 12b: prepend on small → upgrade + data correct */
    {
        ci_str *s = ci_str_small_new("world", 5);
        assert(CI_IS_STR_SMALL(s));
        int r = ci_str_prepend(s, "hello ", 6);
        assert(r == 1);
        assert(!CI_IS_STR_SMALL(s));
        assert(ci_str_len(s) == 11);
        assert(memcmp(ci_str_head(s), "hello world", 11) == 0);
        ci_free(s);
    }

    /* 12c: after upgrade, full ci_str ops work */
    {
        ci_str *s = ci_str_small_new("test", 4);
        ci_str_prepend(s, "XX", 2);
        ci_str_append(s, "YY", 2);
        assert(ci_str_len(s) == 8);
        assert(memcmp(ci_str_head(s), "XXtestYY", 8) == 0);
        ci_str_compact(s);
        assert(ci_str_head_space(s) == 0);
        ci_str_rmhead(s, 2);
        assert(memcmp(ci_str_head(s), "testYY", 6) == 0);
        ci_str_rmtail(s, 2);
        assert(memcmp(ci_str_head(s), "test", 4) == 0);
        uint32_t h = ci_str_hash(s);
        assert(h != 0);
        ci_str_clear(s);
        assert(ci_str_len(s) == 0);
        ci_free(s);
    }

    /* 12d: ensure_head(0) on small — still upgrades */
    {
        ci_str *s = ci_str_small_new("hi", 2);
        uint8_t *p = ci_str_ensure_head(s, 0);
        assert(p != NULL);
        assert(!CI_IS_STR_SMALL(s));
        assert(ci_str_len(s) == 2);
        ci_free(s);
    }

    printf("  section 12 (ensure_head/prepend upgrade): ok\n");

    /* ================================================================
     * Section 13 — upgrade destructor
     * ================================================================ */

    /* 13a: upgraded small string frees malloc'd buffer on ci_free */
    {
        ci_str *s = ci_str_small_new("test", 4);
        ci_str_prepend(s, "XX", 2);
        ci_free(s);
    }

    /* 13b: non-upgraded small string destructor is no-op */
    {
        ci_str *s = ci_str_small_new("test", 4);
        ci_free(s);
    }

    printf("  section 13 (upgrade destructor): ok\n");

    /* ================================================================
     * Section 14 — producer/consumer loop with small string
     * ================================================================ */

    {
        ci_str *s = ci_str_small_new("", 0);
        size_t initial_size = ci_str_size(s);

        for (int i = 0; i < 1000; i++) {
            int r = ci_str_append(s, "AAAA", 4);
            assert(r == 1);
            size_t n = ci_str_rmhead(s, 4);
            assert(n == 4);
            assert(ci_str_len(s) == 0);
            assert(ci_str_head_space(s) == 0);
        }

        assert(ci_str_size(s) == initial_size);
        ci_free(s);
    }

    printf("  section 14 (producer/consumer): ok\n");

    /* ================================================================
     * Section 15 — small string allocation boundaries
     * ================================================================ */

    /* 15a: first size class is 64 */
    {
        ci_str *s = ci_str_small_new("x", 1);
        assert(CI_IS_STR_SMALL(s));
        assert(tg_ptr_size(s) == 64);
        ci_free(s);
    }

    /* 15b: empty string gets 64 */
    {
        ci_str *s = ci_str_small_new("", 0);
        assert(tg_ptr_size(s) == 64);
        ci_free(s);
    }

    /* 15c: max 58 bytes fits in 64-byte pool */
    {
        char buf[64];
        memset(buf, 'X', sizeof(buf));
        ci_str *s = ci_str_small_new(buf, (uint8_t)cap64);
        assert(tg_ptr_size(s) == 64);
        ci_free(s);
    }

    /* 15d: cap64+1 bytes bumps to 128-byte pool */
    {
        char buf[128];
        memset(buf, 'X', sizeof(buf));
        ci_str *s = ci_str_small_new(buf, (uint8_t)(cap64 + 1));
        assert(tg_ptr_size(s) == 128);
        ci_free(s);
    }

    /* 15e: max 122 bytes fits in 128 */
    {
        char buf[128];
        memset(buf, 'X', sizeof(buf));
        ci_str *s = ci_str_small_new(buf, (uint8_t)cap128);
        assert(tg_ptr_size(s) == 128);
        ci_free(s);
    }

    /* 15f: cap128+1 bytes bumps to 256 */
    {
        char buf[256];
        memset(buf, 'X', sizeof(buf));
        ci_str *s = ci_str_small_new(buf, (uint8_t)(cap128 + 1));
        assert(tg_ptr_size(s) == 256);
        ci_free(s);
    }

    /* 15g: max 250 bytes fits in 256 */
    {
        char buf[256];
        memset(buf, 'X', sizeof(buf));
        ci_str *s = ci_str_small_new(buf, (uint8_t)cap256);
        assert(tg_ptr_size(s) == 256);
        ci_free(s);
    }

    /* 15h: cap256+1 → NULL */
    {
        char buf[256];
        memset(buf, 'X', sizeof(buf));
        ci_str *s = ci_str_small_new(buf, (uint8_t)(cap256 + 1));
        assert(s == NULL);
    }

    printf("  section 15 (allocation boundaries): ok\n");

    /* ================================================================
     * Section 16 — null termination in clear (C compat)
     * ================================================================ */

    /* 16a: full ci_str clear null-terminates */
    {
        ci_str *s = ci_str_from_cstr("hello");
        ci_str_clear(s);
        assert(ci_str_head(s)[0] == '\0');
        ci_dec(s);
    }

    /* 16b: small string clear null-terminates */
    {
        ci_str *s = ci_str_small_new("hello", 5);
        ci_str_clear(s);
        assert(ci_str_head(s)[0] == '\0');
        ci_free(s);
    }

    printf("  section 16 (null termination): ok\n");

    (void)cap128;
    (void)cap256;

    teardown();
    printf("test_small_poly: PASSED\n");
    return 0;
}
