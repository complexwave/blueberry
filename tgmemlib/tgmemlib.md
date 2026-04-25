# tgmemlib — tagged arena allocator

## What it is

Single-threaded arena allocator for a scripting language object system.
Each type gets its own chain of 64 KiB slabs. The core trick: arenas are
mmap'd at 64 KiB alignment, so `tg_free()` recovers the arena header from
any object pointer in **one bitop** — no per-object metadata, no allocator
handle needed at free time.

## Files

- `tgmemlib.h` — structs, defines, public API prototypes, inline helpers
- `tgmemlib.c` — implementation; `#include "tgmemlib.c"` to use (no linking)
- `simple_allocator.c` — basic smoke test / usage example

## Key design decisions

**Fixed 64 KiB arenas, power-of-2 aligned.**
Enables `arena = ptr & ARENA_MASK` — the whole point. Variable arena sizes
would break this; not worth the complexity.

**Arena header at byte 0 of mmap region.**
No separate allocation. Header is `tg_arena_t` (48 bytes), objects start at
`ARENA_HDR_SIZE` (rounded to 8).

**Intrusive freelist.**
Free slots store the next pointer in their first word. Requires
`obj_size >= sizeof(void *)`. LIFO order — freed memory reused immediately,
good for cache.

**Bump + freelist, freelist preferred.**
`tg_arena_alloc()` checks freelist first, then bump. When both exhausted,
`tg_alloc()` mmaps a new arena and prepends it to the type's chain.

**obj_size rounded up to 8.**
Enforced in `tg_allocator_register_type()`. Keeps all slots naturally aligned.

**MAX_TYPES = 64.**
Fits comfortably for a scripting language object system (int, float, string,
list, dict, closure, upvalue, ...). Tag is `uint8_t`, fits in 6 bits.

**mmap_aligned trick.**
`mmap(2 * ARENA_SIZE)`, find aligned offset inside, `munmap` prefix and suffix.
Gives ARENA_SIZE-aligned memory without kernel support for aligned mmap.

**Critical bug (fixed).**
`~(ARENA_SIZE - 1)` where ARENA_SIZE is `unsigned int` produces a 32-bit mask,
zeroing the upper 32 bits of a pointer. Must be `~((uintptr_t)ARENA_SIZE - 1)`.
`ARENA_MASK` macro enforces this.

## API

```c
tg_allocator_t *tg_allocator_new(void);
void  tg_allocator_destroy(tg_allocator_t *alloc);

// tag < MAX_TYPES, obj_size >= sizeof(void*), rounded up to 8
void  tg_allocator_register_type(tg_allocator_t *alloc, uint8_t tag, uint16_t obj_size);

// pre-alloc a fresh arena (optional, avoids mid-loop growth)
tg_arena_t *tg_allocator_new_arena(tg_allocator_t *alloc, uint8_t tag);

void *tg_alloc(tg_allocator_t *alloc, uint8_t tag);  // NULL on mmap failure
void  tg_free(void *ptr);                             // no alloc handle needed

// inline helpers
tg_arena_t *tg_ptr_arena(void *ptr);     // ptr -> arena header, one bitop
int         tg_arena_capacity(tg_arena_t *ar); // max objects per arena for this type
```

## Internals (not in header)

```c
static void       *mmap_aligned(size_t size);              // aligned mmap
static tg_arena_t *tg_arena_new(uint8_t tag, uint16_t obj_size);
static void        tg_arena_destroy(tg_arena_t *ar);       // munmap
static void       *tg_arena_alloc(tg_arena_t *ar);         // NULL = full
```

## Arena layout

```
[0 .. ARENA_HDR_SIZE)   tg_arena_t header (48 bytes, padded to 8)
[ARENA_HDR_SIZE .. end) object slots, each obj_size bytes
```

Capacities per arena (64 KiB, 48-byte header):
- 8-byte objects:    8186 slots
- 16-byte objects:   4093 slots
- 1024-byte objects:   63 slots

## What is NOT done yet (next steps)

- **GC.** `tg_free` is per-object freelist only. Bulk arena release
  (walk `types[tag].head` chain, call `tg_arena_destroy` on each) is the
  natural fit for mark-sweep or generational GC. Mark bits could live in
  the arena header.
- **Tests.** Need a proper test file covering:
  - alloc/free/realloc round-trip per type
  - freelist LIFO ordering
  - arena spill (auto-grow on full)
  - `tg_ptr_arena` correctness (tag, obj_size match)
  - `tg_arena_capacity` matches actual fillable slots
  - destroy with multiple arena chains
  - NULL return on mmap failure (needs mmap stub/mock)
  - double-free detection (currently undetected — freelist corruption)
- **Pointer tagging.** The original motivating idea: embed type tag bits
  into the pointer address itself (low bits, since objects are 8-aligned).
  `tg_free` already knows the type from the arena; pointer tags would let
  the interpreter dispatch without a separate tag field in every object.
- **Object header design.** Still TBD — whether objects carry a header word
  at all, or whether type info lives purely in the arena + pointer tag.
