/*
 * test_basic.c — core alloc/free round-trip tests
 */
#include "tgmemlib.c"
#include <stdio.h>
#include <string.h>

#define PASS(name)          do { printf("PASS: %s\n", (name)); } while(0)
#define FAIL(name, reason)  do { printf("FAIL: %s — %s\n", (name), (reason)); return 1; } while(0)

static int test_alloc_non_null(void) {
    tg_allocator_t *alloc = tg_allocator_new();
    tg_allocator_register_type(alloc, 0, 16);
    void *obj = tg_alloc(alloc, 0);
    if (!obj) { tg_allocator_destroy(alloc); FAIL("alloc_non_null", "returned NULL"); }
    tg_allocator_destroy(alloc);
    PASS("alloc_non_null");
    return 0;
}

static int test_field_rw(void) {
    typedef struct { int x; int y; int z; } triple_t;
    tg_allocator_t *alloc = tg_allocator_new();
    tg_allocator_register_type(alloc, 1, sizeof(triple_t));
    triple_t *p = tg_alloc(alloc, 1);
    if (!p) { tg_allocator_destroy(alloc); FAIL("field_rw", "alloc NULL"); }
    p->x = 42; p->y = -7; p->z = 0xdeadbeef;
    if (p->x != 42 || p->y != -7 || p->z != (int)0xdeadbeef) {
        tg_allocator_destroy(alloc);
        FAIL("field_rw", "field mismatch after write");
    }
    tg_allocator_destroy(alloc);
    PASS("field_rw");
    return 0;
}

static int test_ptr_arena_correctness(void) {
    tg_allocator_t *alloc = tg_allocator_new();
    tg_allocator_register_type(alloc, 2, 32);
    void *obj = tg_alloc(alloc, 2);
    if (!obj) { tg_allocator_destroy(alloc); FAIL("ptr_arena_correctness", "alloc NULL"); }
    tg_arena_t *ar = tg_ptr_arena(obj);
    if (ar != alloc->heads[2]) {
        tg_allocator_destroy(alloc);
        FAIL("ptr_arena_correctness", "arena != head");
    }
    if (ar->type_tag != 2) {
        tg_allocator_destroy(alloc);
        FAIL("ptr_arena_correctness", "wrong type_tag");
    }
    /* 32 is already 8-aligned, stays 32 */
    if (ar->obj_size != 32) {
        tg_allocator_destroy(alloc);
        FAIL("ptr_arena_correctness", "wrong obj_size");
    }
    tg_allocator_destroy(alloc);
    PASS("ptr_arena_correctness");
    return 0;
}

static int test_freelist_lifo(void) {
    tg_allocator_t *alloc = tg_allocator_new();
    tg_allocator_register_type(alloc, 3, 8);
    void *A = tg_alloc(alloc, 3);
    void *B = tg_alloc(alloc, 3);
    if (!A || !B) { tg_allocator_destroy(alloc); FAIL("freelist_lifo", "alloc NULL"); }
    tg_free(A);
    tg_free(B);
    void *C = tg_alloc(alloc, 3);  /* should get B: last freed */
    void *D = tg_alloc(alloc, 3);  /* should get A */
    if (C != B) { tg_allocator_destroy(alloc); FAIL("freelist_lifo", "C != B"); }
    if (D != A) { tg_allocator_destroy(alloc); FAIL("freelist_lifo", "D != A"); }
    tg_allocator_destroy(alloc);
    PASS("freelist_lifo");
    return 0;
}

static int test_arena_capacity(void) {
    tg_allocator_t *alloc = tg_allocator_new();
    uint16_t sizes[] = {8, 16, 64, 1024};
    for (int i = 0; i < 4; i++) {
        uint8_t tag = (uint8_t)i;
        tg_allocator_register_type(alloc, tag, sizes[i]);
        tg_allocator_new_arena(alloc, tag);
        tg_arena_t *ar = alloc->heads[tag];
        int cap      = tg_arena_capacity(ar);
        int expected = (int)((ARENA_SIZE - ARENA_HDR_SIZE) / ar->obj_size);
        if (cap != expected) {
            printf("  size=%u cap=%d expected=%d\n", sizes[i], cap, expected);
            tg_allocator_destroy(alloc);
            FAIL("arena_capacity", "mismatch");
        }
    }
    tg_allocator_destroy(alloc);
    PASS("arena_capacity");
    return 0;
}

static int test_allocator_destroy(void) {
    tg_allocator_t *alloc = tg_allocator_new();
    tg_allocator_register_type(alloc, 0, 8);
    tg_allocator_register_type(alloc, 1, 64);
    /* alloc enough to trigger multiple arenas */
    for (int i = 0; i < 10000; i++) tg_alloc(alloc, 0);
    for (int i = 0; i < 2000; i++) tg_alloc(alloc, 1);
    tg_allocator_destroy(alloc);  /* sanitizer/valgrind checks this */
    PASS("allocator_destroy");
    return 0;
}

int main(void) {
    int r = 0;
    r |= test_alloc_non_null();
    r |= test_field_rw();
    r |= test_ptr_arena_correctness();
    r |= test_freelist_lifo();
    r |= test_arena_capacity();
    r |= test_allocator_destroy();
    return r;
}
