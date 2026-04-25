/*
 * test_pattern.c — data integrity: fill objects with byte patterns, verify
 */
#include "tgmemlib.c"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define PASS(name)          do { printf("PASS: %s\n", (name)); } while(0)
#define FAIL(name, reason)  do { printf("FAIL: %s — %s\n", (name), (reason)); return 1; } while(0)

static int verify_pattern(void *obj, int obj_size, unsigned char pat) {
    unsigned char *p = (unsigned char *)obj;
    for (int i = 0; i < obj_size; i++) {
        if (p[i] != pat) return 0;
    }
    return 1;
}

static int test_fill_pattern(void) {
    tg_allocator_t *alloc = tg_allocator_new();
    tg_allocator_register_type(alloc, 0, 16);
    /* alloc enough to span 2 arenas */
    void *first = tg_alloc(alloc, 0);
    if (!first) { tg_allocator_destroy(alloc); FAIL("fill_pattern", "alloc NULL"); }
    int cap = tg_arena_capacity(tg_ptr_arena(first));
    int n = cap * 2;

    void **objs = malloc((size_t)n * sizeof(void *));
    objs[0] = first;
    for (int i = 1; i < n; i++) {
        objs[i] = tg_alloc(alloc, 0);
        if (!objs[i]) { free(objs); tg_allocator_destroy(alloc); FAIL("fill_pattern", "alloc NULL mid-loop"); }
    }
    /* fill each object */
    for (int i = 0; i < n; i++) {
        memset(objs[i], (i & 0xFF), 16);
    }
    /* verify all */
    for (int i = 0; i < n; i++) {
        if (!verify_pattern(objs[i], 16, (unsigned char)(i & 0xFF))) {
            printf("  object %d pattern mismatch\n", i);
            free(objs); tg_allocator_destroy(alloc);
            FAIL("fill_pattern", "pattern mismatch");
        }
    }
    free(objs);
    tg_allocator_destroy(alloc);
    PASS("fill_pattern");
    return 0;
}

static int test_alloc_free_realloc_pattern(void) {
    tg_allocator_t *alloc = tg_allocator_new();
    tg_allocator_register_type(alloc, 0, 32);
    int n = 200;

    void **objs = malloc((size_t)n * sizeof(void *));
    for (int i = 0; i < n; i++) {
        objs[i] = tg_alloc(alloc, 0);
        if (!objs[i]) { free(objs); tg_allocator_destroy(alloc); FAIL("realloc_pattern", "alloc NULL"); }
        memset(objs[i], (i & 0xFF), 32);
    }

    /* free every other object (even indices), keep odd ones */
    int nfreed = 0;
    for (int i = 0; i < n; i += 2) {
        tg_free(objs[i]);
        objs[i] = NULL;
    }

    /* re-alloc into freed slots, write new patterns */
    void **newobjs = malloc((size_t)nfreed * sizeof(void *));
    for (int i = 0; i < nfreed; i++) {
        newobjs[i] = tg_alloc(alloc, 0);
        if (!newobjs[i]) { free(objs); free(newobjs); tg_allocator_destroy(alloc); FAIL("realloc_pattern", "re-alloc NULL"); }
        memset(newobjs[i], (0xAA + i) & 0xFF, 32);
    }

    /* verify odd (untouched) objects still have original patterns */
    for (int i = 1; i < n; i += 2) {
        if (!verify_pattern(objs[i], 32, (unsigned char)(i & 0xFF))) {
            free(objs); free(newobjs); tg_allocator_destroy(alloc);
            FAIL("realloc_pattern", "old object corrupted");
        }
    }
    /* verify new objects have correct new patterns */
    for (int i = 0; i < nfreed; i++) {
        if (!verify_pattern(newobjs[i], 32, (unsigned char)((0xAA + i) & 0xFF))) {
            free(objs); free(newobjs); tg_allocator_destroy(alloc);
            FAIL("realloc_pattern", "new object pattern wrong");
        }
    }

    free(objs);
    free(newobjs);
    tg_allocator_destroy(alloc);
    PASS("realloc_pattern");
    return 0;
}

static int test_large_object_pattern(void) {
    tg_allocator_t *alloc = tg_allocator_new();
    tg_allocator_register_type(alloc, 0, 1024);
    void *first = tg_alloc(alloc, 0);
    if (!first) { tg_allocator_destroy(alloc); FAIL("large_pattern", "alloc NULL"); }
    int cap = tg_arena_capacity(tg_ptr_arena(first));

    void **objs = malloc((size_t)cap * sizeof(void *));
    objs[0] = first;
    for (int i = 1; i < cap; i++) {
        objs[i] = tg_alloc(alloc, 0);
        if (!objs[i]) { free(objs); tg_allocator_destroy(alloc); FAIL("large_pattern", "alloc NULL mid-loop"); }
    }
    /* fill */
    for (int i = 0; i < cap; i++) {
        memset(objs[i], (i & 0xFF), 1024);
    }
    /* verify */
    for (int i = 0; i < cap; i++) {
        if (!verify_pattern(objs[i], 1024, (unsigned char)(i & 0xFF))) {
            printf("  large obj %d corrupted\n", i);
            free(objs); tg_allocator_destroy(alloc);
            FAIL("large_pattern", "pattern mismatch");
        }
    }
    free(objs);
    tg_allocator_destroy(alloc);
    PASS("large_pattern");
    return 0;
}

int main(void) {
    int r = 0;
    r |= test_fill_pattern();
    r |= test_alloc_free_realloc_pattern();
    r |= test_large_object_pattern();
    return r;
}
