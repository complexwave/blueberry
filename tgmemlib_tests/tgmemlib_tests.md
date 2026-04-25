# tgmemlib test plan

Tests live in `tgmemlib_tests/`. Each file is a standalone executable that
`#include "../tgmemlib.c"` directly. Build with `make -C tgmemlib_tests`.

All tests are single-threaded. Exit 0 = pass, non-zero = fail.
Tests print `PASS: <name>` on success, `FAIL: <name> — <reason>` on failure.

**Status: all tests written and passing.**

---

## test_basic.c — core alloc/free round-trip ✓

1. **alloc returns non-NULL** — register type, alloc one object, check != NULL.
2. **field read/write** — alloc a struct, write fields, read them back.
3. **tg_ptr_arena correctness** — alloc object, recover arena via `tg_ptr_arena`,
   verify `type_tag` and `obj_size` match what was registered.
4. **freelist LIFO order** — alloc A then B, free A then B, alloc C then D.
   Expect C == B (last freed = first reused) and D == A.
5. **tg_arena_capacity** — register several sizes (8, 16, 64, 1024), check
   `tg_arena_capacity()` matches `(ARENA_SIZE - ARENA_HDR_SIZE) / obj_size`.
6. **allocator destroy** — create allocator, register types, alloc objects,
   destroy. (Valgrind / sanitizer will catch leaks.)

## test_arena_chain.c — multi-arena growth ✓

1. **exhaust one arena** — alloc exactly `tg_arena_capacity()` objects, verify
   all come from the same arena (same `tg_ptr_arena` base).
2. **spill to second arena** — alloc one more after exhausting first, verify
   `tg_ptr_arena` returns a *different* arena and `alloc->types[tag].head`
   changed.
3. **three+ arenas** — exhaust 3 full arenas, verify chain length is 3 by
   walking `->next`.
4. **mixed types independent** — register two types, exhaust arena for type A,
   verify type B still allocates from its own (separate) arena.
5. **free across arenas** — alloc objects spanning 2 arenas, free some from
   each, re-alloc, verify pointers land back in the correct arenas.

## test_pattern.c — data integrity / consistency ✓

1. **fill pattern** — alloc N objects, fill each with a deterministic byte
   pattern based on index (e.g., `memset(obj, (i & 0xFF), obj_size)`). Then
   walk all objects and verify every byte matches. Catches memory corruption,
   overlapping slots, or arena header clobbering.
2. **alloc-free-realloc pattern check** — alloc N objects with patterns, free
   every other one, alloc again into freed slots, write new patterns, verify
   both old (unfree'd) and new objects have correct data.
3. **large object pattern** — register a type with obj_size=1024, fill entire
   arena, verify patterns. Exercises different slot counts and alignment.

## test_stress.c — randomized alloc/dealloc hammering ✓

1. **random alloc/free mix** — maintain a pool of live pointers (e.g., 100K).
   In a loop: randomly choose alloc or free (biased ~60/40 toward alloc to
   build pressure). On alloc: pick random type, alloc, fill with pattern,
   store in pool. On free: pick random live pointer, verify pattern, free it.
   Run for N iterations (e.g., 500K ops). Catches freelist corruption,
   use-after-free patterns, arena chain bugs.
2. **burst alloc then bulk free** — alloc 50K objects, verify all patterns,
   free all, re-alloc 50K, verify again. Tests arena reuse at scale.
3. **multiple types interleaved** — register 8+ types with different sizes,
   randomly alloc/free across all types. Verify no cross-type contamination
   (pattern includes type tag).

## test_edge.c — edge cases and error paths ✓

1. **zero-alloc destroy** — create allocator, register types, destroy without
   allocating. Must not crash.
2. **register min size** — register with `obj_size = sizeof(void*)` (minimum),
   alloc/free works.
3. **register odd size rounds up** — register with obj_size=9, verify arena's
   `obj_size` is 16 (rounded to 8). Alloc object, verify alignment.
4. **max types** — register all 64 type tags, alloc one from each, verify
   each has correct tag.
5. **capacity boundary** — alloc exactly capacity objects, verify next alloc
   triggers new arena (no off-by-one).
6. **free then re-exhaust** — fill arena, free all objects, then re-alloc all
   from freelist (no new arena created). Verify arena chain length stays 1.
7. **alloc after destroy of other type** — register two types, alloc from both,
   `tg_allocator_destroy` frees everything. (Sanitizer validates no
   use-after-free.)

## test_ptr_arena.c — pointer-to-arena recovery ✓

1. **first slot** — alloc first object in arena, `tg_ptr_arena(obj)` matches
   `alloc->types[tag].head`.
2. **last slot** — alloc until last slot of arena, verify `tg_ptr_arena` still
   correct.
3. **multiple arenas** — objects from different arenas all resolve to their
   own arena header.
4. **after free** — free an object, `tg_ptr_arena` on that address still
   returns the correct arena (pointer is still in-range even if freed).

## test_heavy.c — heavy stress, ~512 MB total allocations ✓

1. **arena_fill** — alloc exactly `capacity_for(tag)` objects of one type in
   one shot, forcing a fresh arena to be created and fully populated.
2. **arena_drain** — pick a random live object, recover its arena via
   `tg_ptr_arena`, scan the live pool and free every object belonging to that
   arena — completely empties the arena back into its freelist.
3. **random single alloc/free** — fills the gaps between bulk ops.

Runs until cumulative bytes allocated >= 512 MB (finishes in ~1-2 s).
Pool of 500K tracked pointers; every alloc is pattern-filled, every free
is verified. Progress printed every 64 MB.

---

## Build

```
make -C tgmemlib_tests        # build + run all
make -C tgmemlib_tests test   # same
make -C tgmemlib_tests clean
```

Each test binary: `test_basic`, `test_arena_chain`, `test_pattern`,
`test_stress`, `test_edge`, `test_ptr_arena`, `test_heavy`.
