/*
 * test_ptr_arena.c — pointer-to-arena recovery correctness
 */
#include "tgmemlib.c"
#include <stdio.h>
#include <stdlib.h>

#define PASS(name)          do { printf("PASS: %s\n", (name)); } while(0)
#define FAIL(name, reason)  do { printf("FAIL: %s — %s\n", (name), (reason)); return 1; } while(0)

static int test_first_slot(void) {
    tg_allocator_t *alloc = tg_allocator_new();
    tg_allocator_register_type(alloc, 0, 16);
    void *obj = tg_alloc(alloc, 0);
    if (!obj) { tg_allocator_destroy(alloc); FAIL("first_slot", "alloc NULL"); }
    tg_arena_t *expected = alloc->heads[0];
    tg_arena_t *got      = tg_ptr_arena(obj);
    if (got != expected) {
        printf("  got=%p expected=%p\n", (void *)got, (void *)expected);
        tg_allocator_destroy(alloc);
        FAIL("first_slot", "arena mismatch");
    }
    tg_allocator_destroy(alloc);
    PASS("first_slot");
    return 0;
}

static int test_last_slot(void) {
    tg_allocator_t *alloc = tg_allocator_new();
    tg_allocator_register_type(alloc, 0, 8);
    void *first = tg_alloc(alloc, 0);
    if (!first) { tg_allocator_destroy(alloc); FAIL("last_slot", "alloc NULL"); }
    tg_arena_t *arena = alloc->heads[0];
    int cap = tg_arena_capacity(arena);

    void *last = first;
    for (int i = 1; i < cap; i++) {
        last = tg_alloc(alloc, 0);
        if (!last) { tg_allocator_destroy(alloc); FAIL("last_slot", "alloc NULL before last"); }
    }
    /* no spill yet */
    if (alloc->heads[0] != arena) {
        tg_allocator_destroy(alloc);
        FAIL("last_slot", "unexpected new arena before last slot");
    }
    tg_arena_t *got = tg_ptr_arena(last);
    if (got != arena) {
        printf("  got=%p expected=%p\n", (void *)got, (void *)arena);
        tg_allocator_destroy(alloc);
        FAIL("last_slot", "last slot resolves wrong arena");
    }
    tg_allocator_destroy(alloc);
    PASS("last_slot");
    return 0;
}

static int test_multiple_arenas(void) {
    tg_allocator_t *alloc = tg_allocator_new();
    tg_allocator_register_type(alloc, 0, 64);

    /* collect one object from each of 3 arenas */
    void *sentinels[3];

    /* fill arena 1 except last slot; take last slot as sentinel */
    void *first = tg_alloc(alloc, 0);
    if (!first) { tg_allocator_destroy(alloc); FAIL("multiple_arenas", "alloc NULL"); }
    tg_arena_t *arena1 = tg_ptr_arena(first);
    int cap = tg_arena_capacity(arena1);
    sentinels[0] = first;
    for (int i = 1; i < cap; i++) tg_alloc(alloc, 0);

    /* spill to arena 2 */
    sentinels[1] = tg_alloc(alloc, 0);
    if (!sentinels[1]) { tg_allocator_destroy(alloc); FAIL("multiple_arenas", "spill 1 NULL"); }
    tg_arena_t *arena2 = tg_ptr_arena(sentinels[1]);
    for (int i = 1; i < cap; i++) tg_alloc(alloc, 0);

    /* spill to arena 3 */
    sentinels[2] = tg_alloc(alloc, 0);
    if (!sentinels[2]) { tg_allocator_destroy(alloc); FAIL("multiple_arenas", "spill 2 NULL"); }
    tg_arena_t *arena3 = tg_ptr_arena(sentinels[2]);

    /* all three arenas distinct */
    if (arena1 == arena2 || arena2 == arena3 || arena1 == arena3) {
        tg_allocator_destroy(alloc);
        FAIL("multiple_arenas", "arenas not distinct");
    }

    /* each sentinel resolves to its own arena */
    if (tg_ptr_arena(sentinels[0]) != arena1) { tg_allocator_destroy(alloc); FAIL("multiple_arenas", "sentinel[0] wrong arena"); }
    if (tg_ptr_arena(sentinels[1]) != arena2) { tg_allocator_destroy(alloc); FAIL("multiple_arenas", "sentinel[1] wrong arena"); }
    if (tg_ptr_arena(sentinels[2]) != arena3) { tg_allocator_destroy(alloc); FAIL("multiple_arenas", "sentinel[2] wrong arena"); }

    tg_allocator_destroy(alloc);
    PASS("multiple_arenas");
    return 0;
}

static int test_after_free(void) {
    tg_allocator_t *alloc = tg_allocator_new();
    tg_allocator_register_type(alloc, 0, 32);
    void *obj = tg_alloc(alloc, 0);
    if (!obj) { tg_allocator_destroy(alloc); FAIL("after_free", "alloc NULL"); }
    tg_arena_t *expected = alloc->heads[0];

    /* free object — address still within the arena's mmap */
    tg_free(obj);

    /* tg_ptr_arena on the freed address must still return the correct arena */
    tg_arena_t *got = tg_ptr_arena(obj);
    if (got != expected) {
        printf("  got=%p expected=%p\n", (void *)got, (void *)expected);
        tg_allocator_destroy(alloc);
        FAIL("after_free", "freed ptr resolves wrong arena");
    }
    if (got->type_tag != 0) { tg_allocator_destroy(alloc); FAIL("after_free", "wrong type_tag post-free"); }
    tg_allocator_destroy(alloc);
    PASS("after_free");
    return 0;
}

int main(void) {
    int r = 0;
    r |= test_first_slot();
    r |= test_last_slot();
    r |= test_multiple_arenas();
    r |= test_after_free();
    return r;
}
