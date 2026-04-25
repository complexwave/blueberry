/*
 * test_edge.c — edge cases and boundary conditions
 */
#include "tgmemlib.c"
#include <stdio.h>
#include <string.h>

#define PASS(name)          do { printf("PASS: %s\n", (name)); } while(0)
#define FAIL(name, reason)  do { printf("FAIL: %s — %s\n", (name), (reason)); return 1; } while(0)

static int test_zero_alloc_destroy(void) {
    tg_allocator_t *alloc = tg_allocator_new();
    tg_allocator_register_type(alloc, 0, 8);
    tg_allocator_register_type(alloc, 1, 64);
    /* destroy without allocating anything — must not crash */
    tg_allocator_destroy(alloc);
    PASS("zero_alloc_destroy");
    return 0;
}

static int test_min_size(void) {
    tg_allocator_t *alloc = tg_allocator_new();
    tg_allocator_register_type(alloc, 0, (uint16_t)sizeof(void *));
    void *p = tg_alloc(alloc, 0);
    if (!p) { tg_allocator_destroy(alloc); FAIL("min_size", "alloc NULL"); }
    *(uintptr_t *)p = 0xdeadbeef;
    if (*(uintptr_t *)p != 0xdeadbeef) { tg_allocator_destroy(alloc); FAIL("min_size", "write/read failed"); }
    tg_free(p);
    tg_allocator_destroy(alloc);
    PASS("min_size");
    return 0;
}

static int test_odd_size_rounds_up(void) {
    tg_allocator_t *alloc = tg_allocator_new();
    tg_allocator_register_type(alloc, 0, 9);
    /* 9 rounded up to 8-alignment = 16 */
    tg_allocator_new_arena(alloc, 0);
    tg_arena_t *ar = alloc->heads[0];
    if (ar->obj_size != 16) {
        printf("  obj_size=%u expected=16\n", ar->obj_size);
        tg_allocator_destroy(alloc);
        FAIL("odd_size_rounds_up", "obj_size not 16");
    }
    void *p = tg_alloc(alloc, 0);
    if (!p) { tg_allocator_destroy(alloc); FAIL("odd_size_rounds_up", "alloc NULL"); }
    if ((uintptr_t)p % 8 != 0) { tg_allocator_destroy(alloc); FAIL("odd_size_rounds_up", "not 8-aligned"); }
    tg_allocator_destroy(alloc);
    PASS("odd_size_rounds_up");
    return 0;
}

static int test_max_types(void) {
    tg_allocator_t *alloc = tg_allocator_new();
    for (int t = 0; t < MAX_TYPES; t++) {
        tg_allocator_register_type(alloc, (uint8_t)t, 8);
    }
    void *ptrs[MAX_TYPES];
    for (int t = 0; t < MAX_TYPES; t++) {
        ptrs[t] = tg_alloc(alloc, (uint8_t)t);
        if (!ptrs[t]) {
            tg_allocator_destroy(alloc);
            FAIL("max_types", "alloc NULL for some tag");
        }
    }
    for (int t = 0; t < MAX_TYPES; t++) {
        tg_arena_t *ar = tg_ptr_arena(ptrs[t]);
        if (ar->type_tag != (uint8_t)t) {
            printf("  tag=%d got type_tag=%u\n", t, ar->type_tag);
            tg_allocator_destroy(alloc);
            FAIL("max_types", "wrong type_tag");
        }
    }
    tg_allocator_destroy(alloc);
    PASS("max_types");
    return 0;
}

static int test_capacity_boundary(void) {
    tg_allocator_t *alloc = tg_allocator_new();
    tg_allocator_register_type(alloc, 0, 8);

    void *first = tg_alloc(alloc, 0);
    if (!first) { tg_allocator_destroy(alloc); FAIL("capacity_boundary", "first alloc NULL"); }
    tg_arena_t *arena1 = tg_ptr_arena(first);
    int cap = tg_arena_capacity(arena1);

    /* fill remaining cap-1 slots */
    for (int i = 1; i < cap; i++) {
        void *p = tg_alloc(alloc, 0);
        if (!p) { tg_allocator_destroy(alloc); FAIL("capacity_boundary", "alloc NULL before capacity"); }
    }
    /* arena should still be arena1 */
    if (alloc->heads[0] != arena1) {
        tg_allocator_destroy(alloc);
        FAIL("capacity_boundary", "new arena created before capacity+1");
    }
    /* one more must trigger new arena */
    void *overflow = tg_alloc(alloc, 0);
    if (!overflow) { tg_allocator_destroy(alloc); FAIL("capacity_boundary", "overflow alloc NULL"); }
    if (alloc->heads[0] == arena1) {
        tg_allocator_destroy(alloc);
        FAIL("capacity_boundary", "no new arena after capacity+1");
    }
    tg_allocator_destroy(alloc);
    PASS("capacity_boundary");
    return 0;
}

static int test_free_reexhaust(void) {
    tg_allocator_t *alloc = tg_allocator_new();
    tg_allocator_register_type(alloc, 0, 8);

    void *first = tg_alloc(alloc, 0);
    if (!first) { tg_allocator_destroy(alloc); FAIL("free_reexhaust", "first alloc NULL"); }
    int cap = tg_arena_capacity(tg_ptr_arena(first));

    void **objs = (void **)malloc((size_t)cap * sizeof(void *));
    objs[0] = first;
    for (int i = 1; i < cap; i++) {
        objs[i] = tg_alloc(alloc, 0);
        if (!objs[i]) { free(objs); tg_allocator_destroy(alloc); FAIL("free_reexhaust", "alloc NULL filling arena"); }
    }

    /* free all */
    for (int i = 0; i < cap; i++) tg_free(objs[i]);

    /* re-alloc all: should come from freelist, no new arena */
    for (int i = 0; i < cap; i++) {
        void *p = tg_alloc(alloc, 0);
        if (!p) { free(objs); tg_allocator_destroy(alloc); FAIL("free_reexhaust", "re-alloc NULL"); }
    }

    /* chain length must still be 1 */
    int len = 0;
    tg_arena_t *ar = alloc->heads[0];
    while (ar) { len++; ar = ar->next; }
    if (len != 1) {
        printf("  chain_len=%d expected=1\n", len);
        free(objs); tg_allocator_destroy(alloc);
        FAIL("free_reexhaust", "chain grew unexpectedly");
    }
    free(objs);
    tg_allocator_destroy(alloc);
    PASS("free_reexhaust");
    return 0;
}

static int test_alloc_after_destroy_other_type(void) {
    tg_allocator_t *alloc = tg_allocator_new();
    tg_allocator_register_type(alloc, 0, 8);
    tg_allocator_register_type(alloc, 1, 32);
    for (int i = 0; i < 100; i++) tg_alloc(alloc, 0);
    for (int i = 0; i < 100; i++) tg_alloc(alloc, 1);
    /* destroy frees everything — sanitizer validates no use-after-free */
    tg_allocator_destroy(alloc);
    PASS("alloc_after_destroy_other_type");
    return 0;
}

int main(void) {
    int r = 0;
    r |= test_zero_alloc_destroy();
    r |= test_min_size();
    r |= test_odd_size_rounds_up();
    r |= test_max_types();
    r |= test_capacity_boundary();
    r |= test_free_reexhaust();
    r |= test_alloc_after_destroy_other_type();
    return r;
}
