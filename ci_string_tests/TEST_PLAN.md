# ci_string comprehensive test plan

## Build & run

Each test file is a standalone C program. It `#include "ciobj.c"` which
transitively pulls in tgmemlib and ci_string. Compiled with `-DCI_STRING_TEST`
to suppress ciobj.c's own main().

```
gcc -O2 -g -Wall -Wextra -I ../ -DCI_STRING_TEST -o test_foo test_foo.c
make test   # builds and runs all
```

## Guidelines for implementing model

- Use `#include "ciobj.c"` (NOT `#include "../ciobj.c"`) — the `-I ../` flag handles paths.
- Every test file has `setup()` calling `ci_init()` + `ci_str_register()` and
  `teardown()` calling `ci_shutdown()`.
- Use `assert()` for all checks. Print test name on success.
- Use `{}` brackets on all if/else/for/while. Exception: `if (x) return y;` one-liners.
- Use `memset` to fill buffers with known patterns (e.g. 'A'+i%26) for data integrity checks.
- When testing polymorphic accessors, cast to `void *` to prove the dispatch works.
- Small strings are NOT refcountable (bit 1 = 0 in their tags). Full ci_str IS refcountable.
- `sizeof(ci_str_small)` = 6 bytes (4-byte gc header + 1-byte length + 1-byte padding).
  Pool capacities: 32-6=26, 64-6=58, 128-6=122, 256-6=250.
- `ci_str_small_new` returns `ci_str *` (not `ci_str_small *`). Small strings are an
  internal optimization — callers always work with `ci_str *`.
- `ci_str_small_new` allocates from 64 / 128 / 256 pools only (32-byte reserved for
  internalized strings). First size class is 64.
- Small strings can be upgraded in-place to full ci_str via `ci_str_upgrade` (internal).
  This is triggered by `ci_str_ensure_head`, `ci_str_prepend`, and `ci_str_clear_headroom`.
  Upgrade fails (stderr + returns NULL) on 32-byte slots (too small for ci_str header)
  and on readonly/internalized strings (CI_IS_READONLY bit 3 in pointer tag).
- After upgrade, the slot is rewritten as ci_str layout. CI_OBJ_SMALL flag is cleared.
  The upgraded string is NOT refcountable (pointer tag bits unchanged). Use ci_free only.
- `ci_str_clear` null-terminates data[0] after clearing (both small and full) for C compat.

---

## Test files

### test_small_basic.c — ci_str_small fundamentals
Tests: `ci_str_small_new`, all polymorphic accessors on small strings, tag macros, free.
- `ci_str_small_new` returns `ci_str *` — all tests use `ci_str *` variables
- Allocate each pool size (64, 128, 256) with fitting data
- `ci_str_len()` returns correct length
- `ci_str_size()` returns slot capacity (slot - sizeof(ci_str_small))
- `ci_str_head()` points to data, `ci_str_tail()` == head + len
- `ci_str_head_space()` always 0 for small strings
- `ci_str_tail_space()` == size - len
- Data integrity via memcmp
- Empty small string (len=0)
- Oversized allocation → NULL
- Tag checks: `CI_IS_ANY_STR` true, `CI_IS_STR_SMALL` true, `CI_IS_STR` false
- Small strings are NOT refcountable: `ci_is_refcountable()` returns 0
- `ci_str_reset_hash()` is no-op (no crash)
- `ci_free()` releases without crash

### test_small_boundaries.c — pool selection edge cases
Tests: exact-fit and overflow boundaries for each small string pool.
- For each pool: allocate at exactly max capacity → correct tag assigned
- For each pool: allocate at max_cap+1 → bumps to next pool (or NULL for 256)
- len=0 → picks smallest pool (32-byte)
- Verify capacity = slot_size - sizeof(ci_str_small) for each pool

### test_str_new.c — ci_str allocation and lifecycle
Tests: `ci_str_new`, tag checks, refcount basics, `ci_free`, `ci_dec`.
- `ci_str_new(N)`: len=0, size>=N, head_space=0, tail_space>=N
- `ci_str_new(0)`: works (allocs 1 byte internally)
- Tag checks: `CI_IS_ANY_STR` true, `CI_IS_STR` true, `CI_IS_STR_SMALL` false
- Refcount starts at 1, `ci_is_refcountable` returns 1
- `ci_inc` → refcnt=2, `ci_dec` → refcnt=1 (not freed, returns 0)
- `ci_dec` from 1 → freed (returns 1)
- `ci_free()` unconditional free works

### test_str_from_cstr.c — ci_str_from_cstr
Tests: creating ci_str from C strings.
- From "hello" → len=5, data matches, eq_cstr true
- From "" → len=0
- From long string (1000 chars) → data integrity byte-by-byte
- No head space on result

### test_str_copy.c — ci_str_copy deep copy
Tests: `ci_str_copy` independence and extra capacity.
- Copy with extra=0: same data, same len, no head space
- Copy with extra=100: tail_space >= 100
- Modify copy → original unchanged (deep copy proof)
- Copy of string with head space → new string has NO head space
- Refcount of copy is 1 (independent object)

### test_accessors_poly.c — polymorphic accessors on both types
Tests: all inline accessors via `void *` on both ci_str and ci_str_small.
- Create one ci_str and one ci_str_small with identical content
- `ci_str_len(void *)` matches for both
- `ci_str_size(void *)` returns correct values
- `ci_str_head(void *)` returns pointer to valid data
- `ci_str_tail(void *)` = head + len
- `ci_str_head_space(void *)`: 0 for small, 0 for fresh ci_str
- `ci_str_tail_space(void *)`: correct for both
- memcmp of head through len bytes matches for both

### test_ensure_tail.c — tail space guarantee and realloc
Tests: `ci_str_ensure_tail` with various growth scenarios.
- New string size=8: ensure_tail(8) → no realloc, returns end ptr
- Fill 8 bytes + ensure_tail(8) → realloc triggered
- Old data preserved after realloc
- ensure_tail(0) → always succeeds
- ensure_tail(1MB) → succeeds, tail_space >= 1MB
- Multiple ensure_tail calls → verify doubling growth policy
- ensure_tail does NOT reset hash (it doesn't change the data window)

### test_put_tail.c — committing tail bytes
Tests: `ci_str_put_tail` pointer advance and hash reset.
- ensure_tail(10), write 5 bytes, put_tail(5) → len increases by 5
- Data readable through new len
- put_tail resets hash: compute hash, put_tail, verify hash==0
- Multiple put_tail calls accumulate correctly
- put_tail(0) → no change (but still resets hash)

### test_ensure_head.c — head space guarantee
Tests: `ci_str_ensure_head` with various scenarios.
- New ci_str(100), write data, ensure_head(10) → retreats start
- Data preserved after ensure_head
- Sufficient head space → just retreats start, no malloc
- Insufficient head space → fresh buffer, data copied, old freed
- After ensure_head with realloc: head_space > requested (doubled headroom)
- ensure_head resets hash
- **Small string upgrade**: ensure_head on small string → upgrades in-place:
  - CI_IS_STR_SMALL becomes false after upgrade
  - Data preserved after upgrade
  - ci_str fields (memory, start, end, limit) valid after upgrade
  - Head space available after upgrade
  - Original pointer still usable (same address, rewritten layout)

### test_compact.c — eliminating head space
Tests: `ci_str_compact` memmove behavior.
- Create string, add headroom via ensure_head, then compact
- After compact: head_space=0, data preserved, len unchanged
- compact on already-compact string → no-op (start already == memory)
- compact resets hash when data moves; does NOT reset when no-op

### test_append.c — ci_str_append
Tests: `ci_str_append` data concatenation.
- Append to empty string → len = appended amount
- Multiple appends → data concatenated in order
- Append triggering realloc → data preserved
- Append 0 bytes → returns 1 (success), no change
- Append large data (> current buffer size) → works correctly
- Append resets hash

### test_prepend.c — ci_str_prepend
Tests: `ci_str_prepend` head insertion.
- Prepend to string with data → new data appears before existing
- Prepend triggering ensure_head realloc → all data preserved
- Multiple prepends → order correct (most recent prepend at head)
- Prepend resets hash
- **Small string upgrade**: prepend on small string triggers upgrade:
  - Data from small string preserved, prepended data appears before it
  - CI_IS_STR_SMALL becomes false after prepend
  - Full ci_str operations (ensure_head, compact, etc.) work after upgrade

### test_rmhead.c — ci_str_rmhead with buffer-drain reset
Tests: `ci_str_rmhead` consumption and auto-reset behavior.
- rmhead(3) on "hello" → len=2, data is "lo"
- rmhead(0) → no change
- rmhead(100) on len=5 → clamped to 5, returns 5, len=0
- rmhead increases head_space (when buffer not fully drained)
- rmhead resets hash
- **DRAIN RESET**: rmhead that consumes ALL data → start and end reset to memory base.
  After rmhead(len), assert start==memory AND end==memory AND head_space==0.
  This is critical for the producer/consumer pattern.

### test_rmtail.c — ci_str_rmtail with buffer-drain reset
Tests: `ci_str_rmtail` consumption and auto-reset behavior.
- rmtail(3) on "hello" → len=2, data is "he"
- rmtail(0) → no change
- rmtail(100) on len=5 → clamped to 5, returns 5, len=0
- rmtail resets hash
- **DRAIN RESET**: rmtail that consumes ALL data → start and end reset to memory base.
  After rmtail(len), assert start==memory AND end==memory AND head_space==0.

### test_clear.c — ci_str_clear / ci_str_clear_headroom
Tests: buffer reset operations.
- clear: len=0, start==memory (verify via head_space==0), hash=0
- clear preserves buffer allocation (size unchanged)
- clear_headroom(10): len=0, head_space=10
- clear_headroom(0) equivalent to clear
- clear_headroom(size) → valid edge case (start at very end, tail_space=0)

### test_hash.c — ci_str_hash FNV-1a
Tests: hash computation, caching, and sentinel behavior.
- Hash of "hello" → consistent value across multiple calls (cached)
- hash==0 before first call, nonzero after
- Two strings with same content → same hash
- Two strings with different content → (very likely) different hash
- Empty string hash: computed and cached (FNV offset basis = 2166136261)
- **SENTINEL**: if FNV-1a naturally produces 0, library stores 1.
  Test: find or construct a string whose raw FNV-1a == 0, verify stored hash == 1.
  (Or just verify the code path: set hash=0, call ci_str_hash, if result==1 the
   sentinel path was taken. For a simpler approach: manually verify the FNV-1a
   formula on known inputs.)

### test_hash_invalidation.c — hash reset on every mutating op
Tests: each operation that changes the data window must reset hash to 0.

Mutating ops (MUST reset hash):
- `ci_str_put_tail` — yes (resets even if n=0)
- `ci_str_ensure_head` — yes (when it actually retreats start)
- `ci_str_compact` — yes (when data moves; no-op if already compact)
- `ci_str_append` — yes
- `ci_str_prepend` — yes
- `ci_str_rmhead` — yes
- `ci_str_rmtail` — yes
- `ci_str_clear` — yes
- `ci_str_clear_headroom` — yes

Non-mutating ops (must NOT reset hash):
- `ci_str_ensure_tail` — NO (only reserves space, doesn't move data window)
- `ci_str_len`, `ci_str_size`, `ci_str_head`, `ci_str_tail` — NO (pure accessors)

For each mutating op: compute hash → call op → assert s->hash == 0.
For ensure_tail: compute hash → call ensure_tail → assert s->hash != 0 (preserved).

### test_eq.c — ci_str_eq / ci_str_eq_cstr corner cases
Tests: equality comparison edge cases.
- Equal strings → 1
- Different lengths → 0
- Same length, different content → 0
- Both empty → 1
- eq_cstr match → 1
- eq_cstr: cstr longer than string → 0
- eq_cstr: cstr shorter than string → 0 (cstr[len] != '\0')
- eq_cstr: empty string vs "" → 1
- eq_cstr: empty string vs "x" → 0
- String with head space (start != memory): eq still works on [start,end)
- Binary data with embedded \0: eq uses memcmp (full length), eq_cstr diverges
  because strncmp stops at \0 — design test to show this difference

### test_syscall_pattern.c — simulated read(2)/write(2) I/O
Tests: the ensure_tail → write → put_tail → rmhead pattern used for I/O.

**Scenario 1: Simulated read(2)**
```
buf = ci_str_new(4096)
tail = ensure_tail(buf, 4096)   // get write pointer
memcpy(tail, fake_data, n)      // simulate read() filling the buffer
put_tail(buf, n)                // commit received bytes
assert len == n, data matches
```

**Scenario 2: Simulated write(2) drain**
```
// buffer has data from scenario 1
head = ci_str_head(buf)         // get read pointer
// simulate write(fd, head, len) — "sent" some bytes
rmhead(buf, sent)               // consume sent bytes
assert len decreased by sent
```

**Scenario 3: Partial read + partial write loop**
Simulate a socket relay: repeatedly "receive" chunks into tail, "send" chunks from head.
Use varying chunk sizes. Verify data integrity after each round.

**Scenario 4: Buffer wraps via drain-reset**
After fully draining (rmhead consumes all), verify pointers reset to base.
Then write again — should succeed without realloc since buffer resets.

### test_producer_consumer.c — dual-callback buffer sharing pattern

**THIS IS THE CRITICAL TEST FOR THE BUFFER-DRAIN-RESET FEATURE.**

Models a common event-loop pattern with two callbacks sharing a ci_str buffer:
- **Producer callback**: writes data to the buffer tail (ensure_tail + memcpy + put_tail)
- **Consumer callback**: reads data from the buffer head (ci_str_head + rmhead)

Without the drain-reset, the start/end pointers drift rightward on every
produce/consume cycle. Eventually start reaches the buffer limit and the
producer must realloc — even though the buffer is empty! The drain-reset
fix (in rmhead/rmtail) resets start=end=memory when the buffer is fully
consumed, so the producer always writes from the base.

**Test sequence:**
1. Create a buffer with a FIXED size (e.g. 256 bytes).
2. Record initial `ci_str_size()`.
3. Loop N times (e.g. 1000 iterations):
   a. Producer: write a chunk (e.g. 64 bytes) to tail.
   b. Consumer: read ALL data from head (rmhead entire length).
   c. Assert: `ci_str_len(buf) == 0`
   d. Assert: `buf->start == buf->memory` (drain-reset happened)
   e. Assert: `buf->end == buf->memory`
   f. Assert: `ci_str_head_space(buf) == 0` (no pointer drift)
4. After all iterations: assert `ci_str_size(buf)` == initial size (NO realloc occurred).

**Variant: partial consumption**
1. Producer writes 100 bytes.
2. Consumer reads 60 bytes (partial).
3. Assert: start advanced by 60, but NOT reset (40 bytes remain).
4. Consumer reads remaining 40 bytes.
5. Assert: drain-reset triggered — start==end==memory.

**Variant: alternating small writes and full drains**
1. Loop: write 1 byte, drain 1 byte. Repeat 10000 times.
2. Assert: no realloc, buffer size unchanged, pointers always reset.

### test_concat_big.c — large string concatenation stress
Tests: building large strings via repeated append/prepend.
- Append 1000 small chunks (e.g. 13 bytes each) → verify total length and data
- Prepend 100 chunks → verify reverse order
- Alternate appends and prepends → verify combined result
- Append a single 1 MB block → data integrity
- Build to 10 MB via repeated 4KB appends → no crash, correct total length

### test_realloc_stress.c — reallocation growth patterns
Tests: realloc behavior under stress.
- Start with size=1, append 1 byte at a time up to 10000 → verify data integrity
- Track ci_str_size() growth → should roughly double each realloc
- ensure_tail with a request larger than double → exact-fit (no doubling)
- Interleave ensure_head and ensure_tail operations → both sides stable
- After many reallocs, compact, verify data integrity

### test_lifecycle_mixed.c — combined operation sequences
Tests: multi-step workflows touching many functions.
- new → append → hash → append more → verify hash invalidated → rehash → eq
- new → prepend → rmhead → compact → verify data
- from_cstr → copy → modify copy → verify original unchanged → eq returns 0
- Allocate 100 strings, free in various orders → no crash
- Small string and full string with same content: ci_str_len agrees via void*
- Full lifecycle: new → append → prepend → rmhead → rmtail → hash → copy → eq → free
