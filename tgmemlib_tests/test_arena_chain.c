/*
 * test_arena_chain.c — multi-arena growth and chain walking
 */
#include "tgmemlib.c"
#include <stdio.h>
#include <stdlib.h>

#define PASS(name)          do { printf("PASS: %s\n", (name)); } while(0)
#define FAIL(name, reason)  do { printf("FAIL: %s — %s\n", (name), (reason)); return 1; } while(0)

/* Walk the arena chain for a tag, return its length */
static int chain_len(tg_allocator_t *alloc, uint8_t tag) {
    int n = 0;
    tg_arena_t *ar = alloc->heads[tag];
    while (ar) { n++; ar = ar->next; }
    return n;
}

static int test_exhaust_one_arena(void) {
    tg_allocator_t *alloc = tg_allocator_new();
    tg_allocator_register_type(alloc, 0, 8);
    /* force first arena to exist */
    void *first = tg_alloc(alloc, 0);
    if (!first) { tg_allocator_destroy(alloc); FAIL("exhaust_one_arena", "first alloc NULL"); }
    tg_arena_t *arena1 = alloc->heads[0];
    int cap = tg_arena_capacity(arena1);

    /* alloc remaining slots (first already consumed one) */
    for (int i = 1; i < cap; i++) {
        void *p = tg_alloc(alloc, 0);
        if (!p) { tg_allocator_destroy(alloc); FAIL("exhaust_one_arena", "alloc returned NULL before capacity"); }
        if (tg_ptr_arena(p) != arena1) {
            tg_allocator_destroy(alloc);
            FAIL("exhaust_one_arena", "object not in expected arena");
        }
    }
    if (chain_len(alloc, 0) != 1) { tg_allocator_destroy(alloc); FAIL("exhaust_one_arena", "chain length != 1"); }
    tg_allocator_destroy(alloc);
    PASS("exhaust_one_arena");
    return 0;
}

static int test_spill_to_second_arena(void) {
    tg_allocator_t *alloc = tg_allocator_new();
    tg_allocator_register_type(alloc, 0, 16);
    void *first = tg_alloc(alloc, 0);
    if (!first) { tg_allocator_destroy(alloc); FAIL("spill_second", "first alloc NULL"); }
    tg_arena_t *arena1 = alloc->heads[0];
    int cap = tg_arena_capacity(arena1);
    for (int i = 1; i < cap; i++) tg_alloc(alloc, 0);

    /* one more: must spill */
    void *spill = tg_alloc(alloc, 0);
    if (!spill) { tg_allocator_destroy(alloc); FAIL("spill_second", "spill alloc NULL"); }
    tg_arena_t *arena2 = alloc->heads[0];
    if (arena2 == arena1) { tg_allocator_destroy(alloc); FAIL("spill_second", "head did not change"); }
    if (tg_ptr_arena(spill) != arena2) { tg_allocator_destroy(alloc); FAIL("spill_second", "spill not in new arena"); }
    if (chain_len(alloc, 0) != 2) { tg_allocator_destroy(alloc); FAIL("spill_second", "chain length != 2"); }
    tg_allocator_destroy(alloc);
    PASS("spill_second");
    return 0;
}

static int test_three_arenas(void) {
    tg_allocator_t *alloc = tg_allocator_new();
    tg_allocator_register_type(alloc, 0, 64);
    /* exhaust 3 full arenas */
    void *any = tg_alloc(alloc, 0);
    if (!any) { tg_allocator_destroy(alloc); FAIL("three_arenas", "alloc NULL"); }
    int cap = tg_arena_capacity(tg_ptr_arena(any));
    for (int i = 1; i < cap * 3; i++) {
        void *p = tg_alloc(alloc, 0);
        if (!p) { tg_allocator_destroy(alloc); FAIL("three_arenas", "alloc NULL mid-loop"); }
    }
    if (chain_len(alloc, 0) != 3) {
        printf("  chain_len=%d expected=3\n", chain_len(alloc, 0));
        tg_allocator_destroy(alloc);
        FAIL("three_arenas", "chain length != 3");
    }
    tg_allocator_destroy(alloc);
    PASS("three_arenas");
    return 0;
}

static int test_mixed_types_independent(void) {
    tg_allocator_t *alloc = tg_allocator_new();
    tg_allocator_register_type(alloc, 0, 8);
    tg_allocator_register_type(alloc, 1, 8);

    /* exhaust type 0's first arena */
    void *first = tg_alloc(alloc, 0);
    if (!first) { tg_allocator_destroy(alloc); FAIL("mixed_independent", "alloc NULL"); }
    int cap = tg_arena_capacity(tg_ptr_arena(first));
    for (int i = 1; i < cap; i++) tg_alloc(alloc, 0);
    /* type 0 arena now full; spill */
    tg_alloc(alloc, 0);

    /* alloc from type 1 — must be in its own fresh arena */
    void *b = tg_alloc(alloc, 1);
    if (!b) { tg_allocator_destroy(alloc); FAIL("mixed_independent", "type 1 alloc NULL"); }
    tg_arena_t *b_ar = tg_ptr_arena(b);
    if (b_ar->type_tag != 1) { tg_allocator_destroy(alloc); FAIL("mixed_independent", "type 1 object has wrong tag"); }
    /* type 1 chain should be length 1 */
    if (chain_len(alloc, 1) != 1) { tg_allocator_destroy(alloc); FAIL("mixed_independent", "type 1 chain != 1"); }
    tg_allocator_destroy(alloc);
    PASS("mixed_independent");
    return 0;
}

static int test_free_across_arenas(void) {
    tg_allocator_t *alloc = tg_allocator_new();
    tg_allocator_register_type(alloc, 0, 8);

    /* fill arena 1 completely */
    void *first = tg_alloc(alloc, 0);
    if (!first) { tg_allocator_destroy(alloc); FAIL("free_across_arenas", "alloc NULL"); }
    tg_arena_t *arena1 = tg_ptr_arena(first);
    int cap = tg_arena_capacity(arena1);

    void **a1_objs = malloc((size_t)cap * sizeof(void *));
    a1_objs[0] = first;
    for (int i = 1; i < cap; i++) a1_objs[i] = tg_alloc(alloc, 0);

    /* spill into arena 2 */
    void *a2_obj = tg_alloc(alloc, 0);
    if (!a2_obj) { free(a1_objs); tg_allocator_destroy(alloc); FAIL("free_across_arenas", "spill alloc NULL"); }
    tg_arena_t *arena2 = tg_ptr_arena(a2_obj);
    if (arena2 == arena1) { free(a1_objs); tg_allocator_destroy(alloc); FAIL("free_across_arenas", "no new arena"); }

    /* free a couple from arena1 (non-head) — they go into arena1's freelist */
    tg_free(a1_objs[0]);
    tg_free(a1_objs[1]);
    /* free the object from arena2 (head) — goes into arena2's freelist */
    tg_free(a2_obj);

    /* re-alloc: tg_alloc draws from head (arena2) freelist first */
    void *r1 = tg_alloc(alloc, 0);
    if (r1 != a2_obj) { free(a1_objs); tg_allocator_destroy(alloc); FAIL("free_across_arenas", "re-alloc not from head freelist"); }
    if (tg_ptr_arena(r1) != arena2) { free(a1_objs); tg_allocator_destroy(alloc); FAIL("free_across_arenas", "re-alloc arena mismatch"); }

    /* old objects in arena1 still resolve correctly */
    for (int i = 2; i < cap; i++) {
        if (tg_ptr_arena(a1_objs[i]) != arena1) {
            free(a1_objs);
            tg_allocator_destroy(alloc);
            FAIL("free_across_arenas", "arena1 object resolves wrong arena");
        }
    }

    free(a1_objs);
    tg_allocator_destroy(alloc);
    PASS("free_across_arenas");
    return 0;
}

int main(void) {
    int r = 0;
    r |= test_exhaust_one_arena();
    r |= test_spill_to_second_arena();
    r |= test_three_arenas();
    r |= test_mixed_types_independent();
    r |= test_free_across_arenas();
    return r;
}
