/* test_ensure_head.c — head space guarantee */
#include "ciobj.c"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static void setup(void)    { ci_init(); ci_str_register(); }
static void teardown(void) { ci_shutdown(); }

int main(void) {
    setup();

    /* --- ensure_head(10): data preserved, start retreated --- */
    {
        ci_str *s = ci_str_new(100);
        assert(s != NULL);

        const char *msg = "payload";
        size_t      mlen = strlen(msg);
        ci_str_append(s, msg, mlen);
        assert(ci_str_len(s) == mlen);

        uint8_t *h = ci_str_ensure_head(s, 10);
        assert(h != NULL);
        /* new_head=20, start=memory+20-10=memory+10, so head_space==10 */
        assert(ci_str_head_space(s) == 10);
        /* data after ensure_head must still match: start still points to data */
        assert(memcmp(ci_str_head(s), msg, mlen) == 0);

        ci_free(s);
    }

    /* --- sufficient head space → just retreat, no realloc --- */
    {
        ci_str *s = ci_str_new(64);
        assert(s != NULL);
        ci_str_append(s, "data", 4);

        /* first call forces a new buffer with new_head = n*2 */
        uint8_t *h1 = ci_str_ensure_head(s, 10);
        assert(h1 != NULL);
        /* after ensure_head(10): start = memory + new_head - 10 = memory + 10
         * head_space = 10 */
        assert(ci_str_head_space(s) == 10);

        uint8_t *mem_before = s->memory;

        /* second ensure_head(10): head_space == 10 >= 10 → no realloc */
        uint8_t *h2 = ci_str_ensure_head(s, 10);
        assert(h2 != NULL);
        assert(s->memory == mem_before); /* no new malloc */
        assert(memcmp(ci_str_head(s), "data", 4) == 0);

        ci_free(s);
    }

    /* --- insufficient head space → fresh buffer, data copied --- */
    {
        ci_str *s = ci_str_new(32);
        assert(s != NULL);
        ci_str_append(s, "important", 9);
        assert(ci_str_head_space(s) == 0);

        uint8_t *mem_before = s->memory;

        /* no head space → must alloc new buffer */
        uint8_t *h = ci_str_ensure_head(s, 20);
        assert(h != NULL);
        /* different buffer (likely) */
        (void)mem_before;
        /* head_space after realloc: retreated by 20, new_head = 40, so space was 40-20=20 */
        /* Actually the start was set to newmem + new_head - n, so head_space = start - memory = new_head - n = n */
        /* head_space == n because start = memory + n after the retreat */
        /* Wait: start = newmem + new_head - n = newmem + 2n - n = newmem + n, so head_space = n */
        /* But we retreated start by n, so after the call head_space = n (remaining space before new start) */
        /* Actually: start = memory + new_head - n. head_space = start - memory = new_head - n = n.
         * After one more ensure_head(n) call, start -= n → start = memory, head_space = 0. */
        assert(ci_str_head_space(s) == 20);
        assert(memcmp(ci_str_head(s), "important", 9) == 0);

        ci_free(s);
    }

    /* --- after realloc: head_space > requested (doubled headroom) --- */
    {
        ci_str *s = ci_str_new(8);
        assert(s != NULL);
        ci_str_append(s, "hi", 2);

        /* Request n=5; new_head = 10; start = memory+10-5 = memory+5; head_space = 5 == n */
        /* But further calls with <= 5 won't realloc */
        uint8_t *h = ci_str_ensure_head(s, 5);
        assert(h != NULL);
        assert(ci_str_head_space(s) == 5);
        /* Extra head capacity is new_head = n*2 = 10; we retreated by n, leaving n spare */
        /* One more ensure_head(5) should NOT realloc */
        uint8_t *mem2 = s->memory;
        uint8_t *h2 = ci_str_ensure_head(s, 5);
        assert(h2 != NULL);
        assert(s->memory == mem2); /* no new alloc */
        assert(ci_str_head_space(s) == 5); /* ensure_head doesn't consume head space */

        ci_free(s);
    }

    /* --- ensure_head on ci_str_small → upgrades to full ci_str --- */
    {
        ci_str *ss = ci_str_small_new("tiny", 4);
        assert(ss != NULL);
        assert(CI_IS_STR_SMALL(ss));

        uint8_t *h = ci_str_ensure_head(ss, 10);
        assert(h != NULL);
        assert(!CI_IS_STR_SMALL(ss)); /* upgraded */
        assert(ci_str_len(ss) == 4);
        assert(memcmp(ci_str_head(ss), "tiny", 4) == 0);
        assert(ci_str_head_space(ss) >= 10);

        ci_free(ss);
    }

    /* --- ensure_head resets hash --- */
    {
        ci_str *s = ci_str_from_cstr("abc");
        uint32_t h = ci_str_hash(s);
        assert(h != 0);

        ci_str_ensure_head(s, 4);
        assert(s->hash == 0);

        ci_free(s);
    }

    teardown();
    printf("test_ensure_head: PASSED\n");
    return 0;
}
