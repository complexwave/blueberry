# ci_array comprehensive test plan

## Build & run

Each test file is a standalone C program. It `#include "ciobj.c"` which
transitively pulls in tgmemlib, ci_string, and ci_array. Compiled with
`-DCI_ARRAY_TEST` to suppress ciobj.c's own main().

```
gcc -O2 -g -Wall -Wextra -I ../ -DCI_ARRAY_TEST -o test_foo test_foo.c
make test   # builds and runs all
```

## Guidelines for implementing tests

- Use `#include "ciobj.c"` (NOT `#include "../ciobj.c"`) — the `-I ../` flag handles paths.
- Every test file has `setup()` calling `ci_init()` + `ci_str_register()` + `ci_arr_register()`
  and `teardown()` calling `ci_shutdown()`.
- Use `assert()` for all checks. Print test name on success.
- Use `{}` brackets on all if/else/for/while. Exception: `if (x) return y;` one-liners.
- For element values: use sentinel pointers (`&dummy[i]`) that are never dereferenced,
  only compared for identity. This avoids needing real objects.
- Inline arrays are NOT refcountable (bit 1 = 0). Full ci_array IS refcountable.
- `ci_arr_new_inline` returns `ci_array *`. Small arrays are an internal optimization.
- After upgrade (inline→full), `CI_OBJ_SMALL` is cleared but pointer tag is unchanged,
  so the upgraded array is NOT refcountable. Use `ci_free` only.
- Circular buffer: `data[(offset + i) % size]` — offset wraps on shift/unshift.
  Tests must verify elements survive when the physical layout wraps around.
- Pool sizes (bytes): 128, 256, 1024, 2048. Header = `offsetof(ci_array, inhdr_data)`.
  Element capacity = `(slot_bytes - header) / sizeof(ci_ptr)`.
- No element refcounting yet — push/pop/set don't ci_inc/ci_dec contained pointers.

---

## Test files

### test_basic.c — fundamentals (IMPLEMENTED)
Tests: `ci_arr_new`, `ci_arr_new_inline`, push/pop, shift/unshift, index/set, clear,
refcount, ensure_space growth, inline upgrade.
- `ci_arr_new(16)`: len=0, size>=16, tag checks, refcountable, refcnt=1
- `ci_arr_new(0)`: works (allocs 1 element internally)
- `ci_arr_new_inline(1)`: CI_OBJ_SMALL set, data==inhdr_data, not refcountable
- Inline capacity for all 4 pools, oversized → NULL
- Push 8 / pop 8 in LIFO order
- Unshift 8 / shift 8 in LIFO order (most recent unshift comes out first)
- Mixed push+unshift, verify logical order via index
- Mixed pop+shift from both ends
- Index random access, set overwrites, neighbors unchanged
- Clear: length=0, offset=0, reusable after
- Inline push/pop/shift
- Refcount: inc/dec on full array
- Push beyond capacity → ensure_space growth, all elements preserved
- Push beyond inline capacity → upgrade, all elements preserved

### test_circular_wrap.c — circular buffer wrap-around correctness
**This is the most critical test. The circular buffer must not lose or reorder
elements when offset wraps around the physical buffer boundary.**

**Scenario 1: shift then push — offset drifts right, data wraps**
```
a = ci_arr_new(8)
push 0..7          → physical: [0 1 2 3 4 5 6 7], offset=0
shift 4 times      → removes 0,1,2,3; offset=4, len=4
push 8,9,10,11     → physical: [8 9 10 11 4 5 6 7], offset=4, len=8
                                ^wrap^
verify index: 4 5 6 7 8 9 10 11
pop 8 → verify 11,10,9,8,7,6,5,4
```

**Scenario 2: unshift wraps offset backward from 0**
```
a = ci_arr_new(8)
push 0..3           → offset=0, len=4
unshift 10,11,12,13 → offset wraps backward: 7,6,5,4
                      physical: [0 1 2 3 _ _ _ _] → [0 1 2 3 13 12 11 10]
                                                      ^         ^offset=4
verify logical: 13 12 11 10 0 1 2 3
verify all via index
shift all 8, verify order
```

**Scenario 3: interleaved shift+push cycling offset through full wrap**
```
a = ci_arr_new(8)
push 8 elements (fill)
repeat 100 times:
    shift 1, push new_value
    verify all 8 elements via index (oldest dropped, newest at tail)
offset wraps around many times; data integrity every iteration
```

**Scenario 4: unshift+pop cycling offset backward**
```
a = ci_arr_new(8)
push 8 elements
repeat 100 times:
    pop 1, unshift new_value
    verify all 8 elements via index
```

**Scenario 5: single-element array — worst case for modular arithmetic**
```
a = ci_arr_new(1)
push(X); assert index(0)==X; pop → X
push(Y); assert index(0)==Y; shift → Y
unshift(Z); assert index(0)==Z; pop → Z
```

### test_realloc_wrap.c — reallocation preserves wrapped circular data
**Critical: when ensure_space reallocs a wrapped buffer like
`[4 5 _ _ _ 1 2 3]` (offset=5, len=5, size=8), the linearized
result must be `[1 2 3 4 5 _ _ _ _ _ _ _ _ _ _ _]`.**

**Scenario 1: realloc with wrap in the middle**
```
a = ci_arr_new(8)
push 0..7 (full)
shift 5           → offset=5, len=3, physical: [_ _ _ _ _ 5 6 7]
push 8,9,10,11,12 → len=8, physical: [8 9 10 11 12 5 6 7], offset=5
                    wrapped: tail portion [8..12] wraps to front
push 13           → triggers realloc (len==size)
verify all 9 elements via index: 5 6 7 8 9 10 11 12 13
verify offset==0 after realloc (linearized)
```

**Scenario 2: realloc with offset at various positions**
For offset in {1, 2, size/2, size-1}:
```
create size=8, fill, shift to desired offset, push back to full
push one more → realloc
verify all elements in correct logical order
```

**Scenario 3: inline array wrap then upgrade**
```
a = ci_arr_new_inline(4)  // gets some capacity
fill to capacity
shift half, push half (create wrap)
push beyond capacity → upgrade + realloc
verify logical order preserved
```

**Scenario 4: ensure_space(N) with large N on wrapped buffer**
```
a = ci_arr_new(8), fill, shift 4, push 4 (wrapped, full)
ci_arr_ensure_space(a, 100)
verify all elements preserved, offset reset to 0
```

### test_inline.c — inline array specifics
- All 4 pool sizes: verify capacity = (slot - header) / sizeof(ci_ptr)
- Exact-fit at each boundary: request exactly cap128 → gets 128 slot
- One over cap128 → bumps to 256 slot
- One over cap2048 → NULL
- Push to full capacity of each pool, verify all via index
- Circular wrap within inline: shift some, push some, verify
- data pointer always == inhdr_data while still inline
- CI_OBJ_SMALL flag set on inline, cleared after upgrade
- After upgrade: data != inhdr_data, CI_OBJ_SMALL cleared
- Destructor: free inline array (no crash, no double-free)
- Destructor after upgrade: frees malloc'd data (no leak)

### test_upgrade.c — inline-to-full upgrade
- Fill inline to capacity, push one more → triggers upgrade
- All pre-upgrade elements preserved in correct order
- CI_OBJ_SMALL cleared after upgrade
- data pointer changed (no longer inhdr_data)
- Push/pop/shift/unshift all work post-upgrade
- Upgrade of wrapped inline buffer: shift some, push some (create wrap), then push beyond → upgrade linearizes correctly
- Upgrade preserves logical order regardless of offset
- Multiple operations after upgrade (ensure it's a normal full array)
- Upgrade of empty inline array → works, len=0

### test_push_pop_stress.c — large-scale push/pop
- Push 10000 elements, verify all via index, pop all in reverse
- Push 10000, shift all in forward order
- Alternate: push 3, pop 1, repeat until 5000 net elements, verify all
- Push 1000, pop 500, push 1000, pop 500 — verify remaining 1000

### test_shift_unshift_stress.c — large-scale head operations
- Unshift 10000 elements, verify all via index, shift all
- Unshift 1000, pop 500, unshift 500 — verify order
- Deque pattern: alternating unshift and pop (FIFO queue from head to tail)

### test_deque_pattern.c — double-ended queue usage
**Models a work-stealing deque or producer-consumer queue.**

**Scenario 1: FIFO via push+shift**
```
for i in 0..999:
    push(i)
for i in 0..999:
    assert shift() == i  (FIFO order)
```

**Scenario 2: FIFO via unshift+pop**
```
for i in 0..999:
    unshift(i)
for i in 0..999:
    assert pop() == i  (FIFO order)
```

**Scenario 3: interleaved producer/consumer**
```
produced = 0, consumed = 0
while consumed < 10000:
    push 1-5 items (produced++)
    shift 1-3 items (consumed++, verify value == consumed sequence)
verify all values consumed in order
```

**Scenario 4: bounded buffer — push then shift in lockstep**
```
a = ci_arr_new(64)
for i in 0..9999:
    push(i)
    v = shift()
    assert v == i
assert len == 0
assert size == 64 (no growth — always 1 element at a time)
```

### test_ensure_space.c — explicit space reservation
- ensure_space(0) on empty → success, no change
- ensure_space(N) on empty → size >= N
- Fill to half, ensure_space(remaining) → no realloc
- Fill to full, ensure_space(1) → realloc, growth policy ~2x
- ensure_space(1000) on size=8 → size >= 1008
- ensure_space after shift (offset != 0): linearizes and resets offset to 0
- ensure_space on inline → upgrades, then grows if needed
- Multiple ensure_space calls: doesn't shrink, only grows

### test_set_index.c — random access edge cases
- Set at index 0 and len-1 (boundaries)
- Set on wrapped array: element at physical wrap point
- Set after shift (offset != 0): verify physical slot is correct
- Read-modify-write: index → modify → set → verify
- After ensure_space/realloc: set still works on correct logical index

### test_clear.c — clear semantics
- Clear full array: len=0, offset=0, size unchanged
- Clear empty array: no-op, no crash
- Clear wrapped array (offset != 0): offset resets to 0
- Clear inline array: len=0, offset=0, data still == inhdr_data
- After clear: push works, starts from offset=0
- Clear doesn't free backing store (full array: data pointer unchanged)

### test_destructor.c — lifecycle and cleanup
- ci_free on full array: no leak (valgrind)
- ci_free on inline array: no crash
- ci_free on upgraded-from-inline: frees malloc'd data
- ci_dec to 0: triggers destructor on full array
- Allocate 100 arrays, free in random order: no crash
- Array containing non-NULL pointers: destructor doesn't try to free elements
  (element refcounting not implemented yet)

### test_mixed_operations.c — combined workflows
- new → push 10 → shift 5 → push 5 → index all → pop all → verify order
- new_inline → fill → upgrade → push more → shift half → verify
- new → push → clear → push different → verify new data
- Create array, fill, ensure_space, push more, pop all → verify full sequence
- Nested: array of "pointers to arrays" (just pointer values, not real refs)

### test_edge_cases.c — boundary conditions
- Array of size 1: push/pop, push/shift, unshift/pop, unshift/shift
- Array of size 2: all 4 operation pairs
- Push to exactly full (no realloc), then push one more (triggers realloc)
- ensure_space(0) when full → no realloc
- ensure_space(UINT32_MAX) → likely OOM, returns 0 gracefully
- Pop/shift return NULL on empty (not crash)
- Index/set are guarded by assert — not tested for out-of-bounds (would abort)
