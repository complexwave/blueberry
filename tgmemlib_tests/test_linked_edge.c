/*
 * test_linked_edge.c — edge cases for contiguous multi-slot allocation
 *
 * Tests tg_arena_alloc_linked directly (static, visible via include).
 */
#include "tgmemlib.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PASS(name)          do { printf("PASS: %s\n", (name)); } while(0)
#define FAIL(name, reason)  do { printf("FAIL: %s — %s\n", (name), (reason)); return 1; } while(0)

/*
 * Fill arena completely, free every second object.
 * Max contiguous run is 1 — asking for 4 must fail.
 * Then free 4 consecutive objects in scrambled order (3,1,4,2).
 * Sort should find the run of 4.
 */
static int test_no_run_then_create_run(void) {
    tg_allocator_t *alloc = tg_allocator_new();
    tg_allocator_register_type(alloc, 0, 16);
    tg_arena_t *ar = alloc->heads[0];

    int cap = tg_arena_capacity(ar);
    void **objs = malloc(cap * sizeof(void *));

    /* fill completely */
    for (int i = 0; i < cap; i++) {
        objs[i] = tg_arena_alloc(ar);
        if (!objs[i]) { free(objs); tg_allocator_destroy(alloc); FAIL("no_run_then_create", "fill failed"); }
    }
    /* bump should be exhausted */
    if (ar->bump + ar->obj_size <= ar->end) {
        free(objs); tg_allocator_destroy(alloc); FAIL("no_run_then_create", "bump not exhausted");
    }

    /* free every second object */
    for (int i = 0; i < cap; i += 2)
        tg_free(objs[i]);

    int expected_live = cap - (cap + 1) / 2;

    /* try alloc 4 contiguous — must fail, max run is 1 */
    void *p = tg_arena_alloc_linked(ar, 4);
    if (p) { free(objs); tg_allocator_destroy(alloc); FAIL("no_run_then_create", "should not find run of 4 in alternating pattern"); }

    /* live_count must be unchanged after failed attempt */
    if (ar->live_count != expected_live) {
        printf("  live_count=%d expected=%d\n", ar->live_count, expected_live);
        free(objs); tg_allocator_destroy(alloc); FAIL("no_run_then_create", "live_count changed after failed alloc");
    }

    /* now free 4 consecutive objects (indices 5,7,9,11 are already free from even pass;
     * pick 4 consecutive odd ones to create a run of 4).
     * Objects at indices 10,11,12,13: 10 is free (even), 11 alive, 12 free, 13 alive.
     * Instead: free objs[11], objs[13] to make 10..13 all free — that's a run of 4.
     * Free in scrambled order: 13, 11, then the already-free 10,12 are on freelist. */
    /* Actually — pick a clean range. objs[20..23]: 20=free, 21=alive, 22=free, 23=alive.
     * Free 21 and 23 in order 23,21 to make 20..23 all free = run of 4. */
    if (cap < 24) {
        free(objs); tg_allocator_destroy(alloc); FAIL("no_run_then_create", "arena too small for test");
    }

    /* free in scrambled order: 23, 21 (the two odd ones in the range 20..23) */
    tg_free(objs[23]);
    tg_free(objs[21]);
    expected_live -= 2;

    /* now indices 20,21,22,23 are all free — sort should find run of 4 */
    p = tg_arena_alloc_linked(ar, 4);
    if (!p) { free(objs); tg_allocator_destroy(alloc); FAIL("no_run_then_create", "should find run of 4 after freeing consecutive"); }

    /* verify it starts at objs[20] — the lowest of the 4 */
    if (p != objs[20]) {
        printf("  got=%p expected=%p (objs[20])\n", p, objs[20]);
        free(objs); tg_allocator_destroy(alloc); FAIL("no_run_then_create", "run does not start at expected slot");
    }

    /* write across all 4 slots to prove they're usable */
    memset(p, 0xDD, 4 * ar->obj_size);

    free(objs);
    tg_allocator_destroy(alloc);
    PASS("no_run_then_create");
    return 0;
}

/*
 * Free exactly 4 objects in order 3,1,4,2 (1-based → indices 2,0,3,1).
 * Sort must reassemble the contiguous run.
 */
static int test_scrambled_free_order(void) {
    tg_allocator_t *alloc = tg_allocator_new();
    tg_allocator_register_type(alloc, 0, 32);
    tg_arena_t *ar = alloc->heads[0];

    /* alloc 8 objects, we'll work with the first 4 */
    void *objs[8];
    for (int i = 0; i < 8; i++) {
        objs[i] = tg_arena_alloc(ar);
        if (!objs[i]) { tg_allocator_destroy(alloc); FAIL("scrambled_free", "alloc failed"); }
    }

    /* free in order: 3rd, 1st, 4th, 2nd (0-indexed: 2, 0, 3, 1) */
    tg_free(objs[2]);
    tg_free(objs[0]);
    tg_free(objs[3]);
    tg_free(objs[1]);

    /* freelist is now: 1 → 3 → 0 → 2 → NULL (LIFO) — maximally scrambled */

    void *p = tg_arena_alloc_linked(ar, 4);
    if (!p) { tg_allocator_destroy(alloc); FAIL("scrambled_free", "should find run of 4"); }

    if (p != objs[0]) {
        printf("  got=%p expected=%p (objs[0])\n", p, objs[0]);
        tg_allocator_destroy(alloc); FAIL("scrambled_free", "run should start at objs[0]");
    }

    /* verify contiguity: objs[0] through objs[3] are each obj_size apart */
    for (int i = 0; i < 3; i++) {
        if ((char *)objs[i] + ar->obj_size != (char *)objs[i + 1]) {
            tg_allocator_destroy(alloc); FAIL("scrambled_free", "original objs not contiguous");
        }
    }

    memset(p, 0xEE, 4 * ar->obj_size);

    tg_allocator_destroy(alloc);
    PASS("scrambled_free");
    return 0;
}

/*
 * Request run larger than what freelist has, but bump can serve.
 * Freelist has 3 scattered (non-contiguous), bump has space for 5.
 */
static int test_freelist_too_small_bump_serves(void) {
    tg_allocator_t *alloc = tg_allocator_new();
    tg_allocator_register_type(alloc, 0, 16);
    tg_arena_t *ar = alloc->heads[0];

    /* alloc 6 objects */
    void *objs[6];
    for (int i = 0; i < 6; i++)
        objs[i] = tg_arena_alloc(ar);

    /* free 3 non-adjacent ones: 0, 2, 4 */
    tg_free(objs[0]);
    tg_free(objs[2]);
    tg_free(objs[4]);

    /* ask for 3 contiguous — freelist has no adjacent pair, bump should serve */
    void *p = tg_arena_alloc_linked(ar, 3);
    if (!p) { tg_allocator_destroy(alloc); FAIL("bump_serves", "returned NULL"); }

    /* should come from bump, not freelist — address after objs[5] */
    if ((char *)p <= (char *)objs[5]) {
        tg_allocator_destroy(alloc); FAIL("bump_serves", "expected bump region, got freelist region");
    }

    tg_allocator_destroy(alloc);
    PASS("bump_serves");
    return 0;
}

/*
 * Empty freelist, bump has space — linked alloc from bump.
 */
static int test_empty_freelist_bump(void) {
    tg_allocator_t *alloc = tg_allocator_new();
    tg_allocator_register_type(alloc, 0, 64);
    tg_arena_t *ar = alloc->heads[0];

    if (ar->freelist) { tg_allocator_destroy(alloc); FAIL("empty_freelist_bump", "freelist not empty"); }

    void *p = tg_arena_alloc_linked(ar, 5);
    if (!p) { tg_allocator_destroy(alloc); FAIL("empty_freelist_bump", "returned NULL"); }
    if (ar->live_count != 5) { tg_allocator_destroy(alloc); FAIL("empty_freelist_bump", "live_count != 5"); }

    /* next single alloc should come right after */
    void *q = tg_arena_alloc(ar);
    if ((char *)q != (char *)p + 5 * ar->obj_size) {
        tg_allocator_destroy(alloc); FAIL("empty_freelist_bump", "next alloc not contiguous with linked");
    }

    tg_allocator_destroy(alloc);
    PASS("empty_freelist_bump");
    return 0;
}

int main(void) {
    int r = 0;
    r |= test_no_run_then_create_run();
    r |= test_scrambled_free_order();
    r |= test_freelist_too_small_bump_serves();
    r |= test_empty_freelist_bump();
    return r;
}
