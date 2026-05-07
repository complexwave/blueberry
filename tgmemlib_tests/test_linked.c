/*
 * test_linked.c — contiguous multi-slot (linked) allocation tests
 */
#include "tgmemlib.c"
#include <stdio.h>
#include <string.h>

#define PASS(name)          do { printf("PASS: %s\n", (name)); } while(0)
#define FAIL(name, reason)  do { printf("FAIL: %s — %s\n", (name), (reason)); return 1; } while(0)

/* test: basic linked alloc returns non-null, slots are contiguous */
static int test_linked_basic(void) {
    tg_allocator_t *alloc = tg_allocator_new();
    tg_allocator_register_type(alloc, 0, 32);
    void *p = tg_alloc_linked(alloc, 0, 3 * 32);
    if (!p) { tg_allocator_destroy(alloc); FAIL("linked_basic", "returned NULL"); }

    tg_arena_t *ar = tg_ptr_arena(p);
    /* slots should be contiguous: p, p+32, p+64 */
    char *s0 = (char *)p;
    if (s0 + 32 + 32 > ar->end) {
        tg_allocator_destroy(alloc); FAIL("linked_basic", "slots exceed arena");
    }

    /* write to all three slots */
    memset(s0,      0xAA, 32);
    memset(s0 + 32, 0xBB, 32);
    memset(s0 + 64, 0xCC, 32);
    if (*(unsigned char *)s0 != 0xAA || *(unsigned char *)(s0 + 32) != 0xBB ||
        *(unsigned char *)(s0 + 64) != 0xCC) {
        tg_allocator_destroy(alloc); FAIL("linked_basic", "data mismatch");
    }

    tg_allocator_destroy(alloc);
    PASS("linked_basic");
    return 0;
}

/* test: live_count tracks correctly for linked alloc/free */
static int test_linked_livecount(void) {
    tg_allocator_t *alloc = tg_allocator_new();
    tg_allocator_register_type(alloc, 0, 16);

    void *a = tg_alloc(alloc, 0);
    tg_arena_t *ar = tg_ptr_arena(a);
    if (ar->live_count != 1) { tg_allocator_destroy(alloc); FAIL("linked_livecount", "count != 1 after single alloc"); }

    void *p = tg_alloc_linked(alloc, 0, 4 * 16);
    if (!p) { tg_allocator_destroy(alloc); FAIL("linked_livecount", "linked alloc NULL"); }
    if (ar->live_count != 5) { tg_allocator_destroy(alloc); FAIL("linked_livecount", "count != 5 after linked alloc"); }

    /* simulate destructor: free_linked returns extra 3, tg_free returns first */
    tg_free_linked(p, 4 * 16);   /* 4 slots × 16 bytes — frees slots 1..3 */
    if (ar->live_count != 2) { tg_allocator_destroy(alloc); FAIL("linked_livecount", "count != 2 after linked free"); }

    /* tg_free handles slot 0 (would call destructor in real use) */
    tg_free(p);
    if (ar->live_count != 1) { tg_allocator_destroy(alloc); FAIL("linked_livecount", "count != 1 after free first slot"); }

    tg_free(a);
    if (ar->live_count != 0) { tg_allocator_destroy(alloc); FAIL("linked_livecount", "count != 0 after full free"); }

    tg_allocator_destroy(alloc);
    PASS("linked_livecount");
    return 0;
}

/* test: linked alloc from freelist — alloc objects, free some, then linked-alloc should
 * find contiguous run via bubble sort */
static int test_linked_from_freelist(void) {
    tg_allocator_t *alloc = tg_allocator_new();
    tg_allocator_register_type(alloc, 0, 16);

    /* alloc 10 objects — they come from bump so they're contiguous */
    void *objs[10];
    for (int i = 0; i < 10; i++) {
        objs[i] = tg_alloc(alloc, 0);
        if (!objs[i]) { tg_allocator_destroy(alloc); FAIL("linked_from_freelist", "alloc NULL"); }
    }

    /* free 5 contiguous ones (indices 3..7) in reverse order so freelist is scrambled */
    tg_free(objs[7]);
    tg_free(objs[5]);
    tg_free(objs[3]);
    tg_free(objs[6]);
    tg_free(objs[4]);

    /* now ask for 3 contiguous — sort should find a run within [3..7] */
    void *linked = tg_alloc_linked(alloc, 0, 3 * 16);
    if (!linked) { tg_allocator_destroy(alloc); FAIL("linked_from_freelist", "linked alloc NULL"); }

    /* verify contiguity */
    char *base = (char *)linked;
    tg_arena_t *ar = tg_ptr_arena(linked);
    /* the returned pointer should be one of objs[3..5] and span 3 slots */
    int found = 0;
    for (int i = 3; i <= 5; i++) {
        if (base == (char *)objs[i]) { found = 1; break; }
    }
    if (!found) { tg_allocator_destroy(alloc); FAIL("linked_from_freelist", "not in expected range"); }

    /* write across all 3 slots */
    memset(base, 0x42, 3 * ar->obj_size);

    tg_allocator_destroy(alloc);
    PASS("linked_from_freelist");
    return 0;
}

/* test: linked alloc with count=1 behaves like normal alloc */
static int test_linked_single(void) {
    tg_allocator_t *alloc = tg_allocator_new();
    tg_allocator_register_type(alloc, 0, 24);

    void *p = tg_alloc_linked(alloc, 0, 1 * 24);
    if (!p) { tg_allocator_destroy(alloc); FAIL("linked_single", "returned NULL"); }

    tg_arena_t *ar = tg_ptr_arena(p);
    if (ar->live_count != 1) { tg_allocator_destroy(alloc); FAIL("linked_single", "live_count != 1"); }

    /* single slot: free_linked frees 0 extra, tg_free frees the slot */
    tg_free_linked(p, 24);   /* 1 slot × 24 bytes — frees 0 extra */
    if (ar->live_count != 1) { tg_allocator_destroy(alloc); FAIL("linked_single", "live_count changed after free_linked(1 slot)"); }
    tg_free(p);
    if (ar->live_count != 0) { tg_allocator_destroy(alloc); FAIL("linked_single", "live_count != 0 after free"); }

    tg_allocator_destroy(alloc);
    PASS("linked_single");
    return 0;
}

/* test: linked alloc falls through to new arena when current is too fragmented */
static int test_linked_new_arena(void) {
    tg_allocator_t *alloc = tg_allocator_new();
    tg_allocator_register_type(alloc, 0, 16);
    tg_arena_t *ar = alloc->heads[0];

    /* fill the arena completely via bump */
    int cap = tg_arena_capacity(ar);
    void **objs = malloc(cap * sizeof(void *));
    for (int i = 0; i < cap; i++) {
        objs[i] = tg_alloc(alloc, 0);
        if (!objs[i]) { free(objs); tg_allocator_destroy(alloc); FAIL("linked_new_arena", "fill alloc NULL"); }
    }

    /* free every other one — no contiguous pair exists */
    for (int i = 0; i < cap; i += 2)
        tg_free(objs[i]);

    /* request 2 contiguous — freelist has no adjacent pair, bump exhausted → new arena */
    void *linked = tg_alloc_linked(alloc, 0, 2 * 16);
    if (!linked) { free(objs); tg_allocator_destroy(alloc); FAIL("linked_new_arena", "returned NULL"); }

    /* should be in a different arena */
    tg_arena_t *ar2 = tg_ptr_arena(linked);
    if (ar2 == ar) { free(objs); tg_allocator_destroy(alloc); FAIL("linked_new_arena", "same arena — expected new"); }

    free(objs);
    tg_allocator_destroy(alloc);
    PASS("linked_new_arena");
    return 0;
}

/* test: free_linked returns slots to freelist, they can be re-allocated individually */
static int test_free_linked_reuse(void) {
    tg_allocator_t *alloc = tg_allocator_new();
    tg_allocator_register_type(alloc, 0, 16);

    void *p = tg_alloc_linked(alloc, 0, 3 * 16);
    if (!p) { tg_allocator_destroy(alloc); FAIL("free_linked_reuse", "alloc NULL"); }

    /* free_linked returns extra 2, tg_free returns first */
    tg_free_linked(p, 3 * 16);
    tg_free(p);

    /* re-alloc individually — should get slots back from freelist */
    void *a = tg_alloc(alloc, 0);
    void *b = tg_alloc(alloc, 0);
    void *c = tg_alloc(alloc, 0);
    if (!a || !b || !c) { tg_allocator_destroy(alloc); FAIL("free_linked_reuse", "realloc NULL"); }

    /* all three should be within the original linked range */
    char *base = (char *)p;
    tg_arena_t *ar = tg_ptr_arena(p);
    int in_range = 0;
    char *ptrs[3] = { a, b, c };
    for (int i = 0; i < 3; i++) {
        if (ptrs[i] >= base && ptrs[i] < base + 3 * ar->obj_size)
            in_range++;
    }
    if (in_range != 3) { tg_allocator_destroy(alloc); FAIL("free_linked_reuse", "realloc not from freed range"); }

    tg_allocator_destroy(alloc);
    PASS("free_linked_reuse");
    return 0;
}

int main(void) {
    int r = 0;
    r |= test_linked_basic();
    r |= test_linked_livecount();
    r |= test_linked_from_freelist();
    r |= test_linked_single();
    r |= test_linked_new_arena();
    r |= test_free_linked_reuse();
    return r;
}
