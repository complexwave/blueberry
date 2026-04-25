/* test_accessors_poly.c — polymorphic accessors on both types via void* */
#include "ciobj.c"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static void setup(void)    { ci_init(); ci_str_register(); }
static void teardown(void) { ci_shutdown(); }

int main(void) {
    setup();

    const char *content = "polymorph";
    size_t      clen    = strlen(content);  /* 9 */

    ci_str *fs = ci_str_from_cstr(content);
    ci_str *ss = ci_str_small_new(content, (uint8_t)clen);
    assert(fs != NULL);
    assert(ss != NULL);

    void *fv = (void *)fs;
    void *sv = (void *)ss;

    size_t cap64 = 64 - sizeof(ci_str_small);

    /* --- ci_str_len --- */
    assert(ci_str_len(fv) == clen);
    assert(ci_str_len(sv) == clen);

    /* --- ci_str_size --- */
    assert(ci_str_size(fv) >= clen);
    assert(ci_str_size(sv) == cap64); /* smallest pool (64-byte) */

    /* --- ci_str_head --- */
    assert(ci_str_head(fv) != NULL);
    assert(ci_str_head(sv) != NULL);
    assert(memcmp(ci_str_head(fv), content, clen) == 0);
    assert(memcmp(ci_str_head(sv), content, clen) == 0);

    /* --- ci_str_tail --- */
    assert(ci_str_tail(fv) == ci_str_head(fv) + clen);
    assert(ci_str_tail(sv) == ci_str_head(sv) + clen);

    /* --- ci_str_head_space --- */
    assert(ci_str_head_space(fv) == 0); /* fresh ci_str, no headroom */
    assert(ci_str_head_space(sv) == 0); /* always 0 for small */

    /* --- ci_str_tail_space --- */
    assert(ci_str_tail_space(fv) == ci_str_size(fv) - clen);
    assert(ci_str_tail_space(sv) == cap64 - clen);

    /* --- memcmp of data window --- */
    assert(memcmp(ci_str_head(fv), ci_str_head(sv), clen) == 0);

    ci_free(fs);
    ci_free(ss);

    teardown();
    printf("test_accessors_poly: PASSED\n");
    return 0;
}
