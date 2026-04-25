# small string polymorphism test plan

## Key facts

- `ci_str_small_new` returns `ci_str *`. Callers never see `ci_str_small`.
- All function signatures take `ci_str *`. Internal dispatch via `CI_IS_STR_SMALL`.
- `sizeof(ci_str_small)` = 6 (4 gc header + 1 length + 1 padding).
- Pool capacities: 64-6=58, 128-6=122, 256-6=250.
- `ci_str_small_new` selects from 64 / 128 / 256 only (32-byte reserved for internalized).
- Small strings are NOT refcountable. Use `ci_free` only, not `ci_dec`.
- Upgrade: `ci_str_ensure_head`, `ci_str_prepend`, `ci_str_clear_headroom` trigger
  in-place upgrade from small to full ci_str (slot rewritten, CI_OBJ_SMALL cleared,
  malloc'd buffer allocated). Upgrade fails on 32-byte slots and readonly strings.
- After upgrade, pointer is same address but layout is ci_str. Still not refcountable.

## Expected behaviour per function

| function              | on ci_str_small                                              |
|-----------------------|--------------------------------------------------------------|
| `ci_str_ensure_tail`  | if tail_space >= n: return data+length; else return NULL     |
| `ci_str_put_tail`     | sm->length += n                                              |
| `ci_str_compact`      | no-op (small strings are always compact)                     |
| `ci_str_append`       | if fits in slot: memcpy + length += len; else return 0       |
| `ci_str_rmhead`       | clamp, memmove data left, length -= n                        |
| `ci_str_rmtail`       | clamp, length -= n                                           |
| `ci_str_clear`        | length = 0, data[0] = '\0'                                   |
| `ci_str_clear_headroom` | **upgrades** small to full, then sets headroom             |
| `ci_str_hash`         | compute FNV-1a over data[0..length) each call, no caching    |
| `ci_str_eq`           | uses ci_str_head/ci_str_len — works for any combination      |
| `ci_str_eq_cstr`      | uses ci_str_head/ci_str_len — works                          |
| `ci_str_copy`         | uses ci_str_head(src) — works for small source               |
| `ci_str_ensure_head`  | **upgrades** small to full, then proceeds normally           |
| `ci_str_prepend`      | calls ensure_head → **upgrade**, then prepends               |

## Test file: test_small_poly.c

Single test file covering all small string polymorphic behavior.
Uses `ci_str *` for all variables. Each section prints name on success.

### Build

```
gcc -O2 -g -Wall -Wextra -I ../ -DCI_STRING_TEST -o test_small_poly test_small_poly.c
```

---

## Section 1 — ensure_tail on small string

### 1a: sufficient tail space → returns correct pointer
```
s = ci_str_small_new("hi", 2)           // 64-byte slot, cap=58
p = ci_str_ensure_tail(s, 1)
assert p != NULL
assert p == ci_str_tail(s)
assert ci_str_len(s) == 2               // unchanged
ci_free(s)
```

### 1b: exceeds slot capacity → returns NULL
```
s = ci_str_small_new("", 0)             // 64-byte slot, cap=58
p = ci_str_ensure_tail(s, 59)
assert p == NULL
assert ci_str_len(s) == 0               // unchanged
ci_free(s)
```

### 1c: exactly at capacity → returns pointer
```
s = ci_str_small_new("", 0)             // 64-byte slot, cap=58
p = ci_str_ensure_tail(s, 58)
assert p != NULL
ci_free(s)
```

---

## Section 2 — put_tail on small string

### 2a: write bytes then commit
```
s = ci_str_small_new("AB", 2)
p = ci_str_ensure_tail(s, 3)
memcpy(p, "XYZ", 3)
ci_str_put_tail(s, 3)
assert ci_str_len(s) == 5
assert memcmp(ci_str_head(s), "ABXYZ", 5) == 0
ci_free(s)
```

### 2b: put_tail(0) — no change
```
s = ci_str_small_new("hello", 5)
ci_str_put_tail(s, 0)
assert ci_str_len(s) == 5
ci_free(s)
```

---

## Section 3 — compact on small string

### 3a: no-op, data unchanged
```
s = ci_str_small_new("hello", 5)
ci_str_compact(s)
assert ci_str_len(s) == 5
assert memcmp(ci_str_head(s), "hello", 5) == 0
ci_free(s)
```

---

## Section 4 — append on small string

### 4a: fits → success
```
s = ci_str_small_new("AB", 2)
r = ci_str_append(s, "CD", 2)
assert r == 1
assert ci_str_len(s) == 4
assert memcmp(ci_str_head(s), "ABCD", 4) == 0
ci_free(s)
```

### 4b: append to empty
```
s = ci_str_small_new("", 0)
r = ci_str_append(s, "hello", 5)
assert r == 1
assert ci_str_len(s) == 5
ci_free(s)
```

### 4c: append 0 bytes
```
s = ci_str_small_new("hi", 2)
r = ci_str_append(s, "X", 0)
assert r == 1
assert ci_str_len(s) == 2
ci_free(s)
```

### 4d: overflow → returns 0, unchanged
```
s = ci_str_small_new("", 0)             // cap=58
r = ci_str_append(s, <59-byte buf>, 59)
assert r == 0
assert ci_str_len(s) == 0
ci_free(s)
```

### 4e: repeated appends filling slot exactly
```
s = ci_str_small_new("", 0)             // cap=58
for i in 0..58:
    r = ci_str_append(s, &byte, 1)
    assert r == 1
assert ci_str_len(s) == 58
r = ci_str_append(s, &byte, 1)          // one more: must fail
assert r == 0
assert ci_str_len(s) == 58
ci_free(s)
```

---

## Section 5 — rmtail on small string

### 5a: basic rmtail
```
s = ci_str_small_new("hello", 5)
n = ci_str_rmtail(s, 3)
assert n == 3
assert ci_str_len(s) == 2
assert memcmp(ci_str_head(s), "he", 2) == 0
ci_free(s)
```

### 5b: rmtail(0)
```
s = ci_str_small_new("hello", 5)
n = ci_str_rmtail(s, 0)
assert n == 0
assert ci_str_len(s) == 5
ci_free(s)
```

### 5c: clamped
```
s = ci_str_small_new("hi", 2)
n = ci_str_rmtail(s, 100)
assert n == 2
assert ci_str_len(s) == 0
ci_free(s)
```

### 5d: drain all then reuse
```
s = ci_str_small_new("hello", 5)
ci_str_rmtail(s, 5)
assert ci_str_len(s) == 0
r = ci_str_append(s, "X", 1)
assert r == 1
assert ci_str_len(s) == 1
ci_free(s)
```

---

## Section 6 — rmhead on small string (memmove)

### 6a: basic rmhead — data shifts left
```
s = ci_str_small_new("hello", 5)
n = ci_str_rmhead(s, 2)
assert n == 2
assert ci_str_len(s) == 3
assert memcmp(ci_str_head(s), "llo", 3) == 0
ci_free(s)
```

### 6b: rmhead(0)
```
s = ci_str_small_new("hello", 5)
n = ci_str_rmhead(s, 0)
assert n == 0
assert ci_str_len(s) == 5
ci_free(s)
```

### 6c: clamped
```
s = ci_str_small_new("hi", 2)
n = ci_str_rmhead(s, 100)
assert n == 2
assert ci_str_len(s) == 0
ci_free(s)
```

### 6d: drain all then reuse
```
s = ci_str_small_new("hello", 5)
ci_str_rmhead(s, 5)
assert ci_str_len(s) == 0
r = ci_str_append(s, "XY", 2)
assert r == 1
assert memcmp(ci_str_head(s), "XY", 2) == 0
ci_free(s)
```

### 6e: partial rmhead twice
```
s = ci_str_small_new("ABCDE", 5)
ci_str_rmhead(s, 2)                     // "CDE"
assert ci_str_len(s) == 3
assert memcmp(ci_str_head(s), "CDE", 3) == 0
ci_str_rmhead(s, 3)                     // ""
assert ci_str_len(s) == 0
ci_free(s)
```

### 6f: head_space always 0 after rmhead
```
s = ci_str_small_new("hello", 5)
ci_str_rmhead(s, 3)
assert ci_str_head_space(s) == 0
ci_free(s)
```

---

## Section 7 — clear on small string

### 7a: sets length to 0, null-terminates
```
s = ci_str_small_new("hello", 5)
ci_str_clear(s)
assert ci_str_len(s) == 0
assert ci_str_tail_space(s) == ci_str_size(s)
assert ci_str_head(s)[0] == '\0'         // null-terminated for C compat
ci_free(s)
```

### 7b: clear then reuse
```
s = ci_str_small_new("hello", 5)
ci_str_clear(s)
r = ci_str_append(s, "new", 3)
assert r == 1
assert ci_str_len(s) == 3
assert memcmp(ci_str_head(s), "new", 3) == 0
ci_free(s)
```

---

## Section 8 — clear_headroom on small string (upgrade)

`ci_str_clear_headroom` upgrades small strings in-place before setting headroom.

### 8a: clear_headroom triggers upgrade
```
s = ci_str_small_new("hello", 5)        // 64-byte slot
ci_str_clear_headroom(s, 10)
assert !CI_IS_STR_SMALL(s)              // upgraded
assert ci_str_len(s) == 0
assert ci_str_head_space(s) == 10
ci_free(s)
```

### 8b: clear_headroom(0) still upgrades (headroom concept is full-string-only)
```
s = ci_str_small_new("hello", 5)
ci_str_clear_headroom(s, 0)
assert !CI_IS_STR_SMALL(s)              // upgraded
assert ci_str_len(s) == 0
ci_free(s)
```

---

## Section 9 — hash on small string

No caching — FNV-1a computed on every call.

### 9a: nonzero for non-empty
```
s = ci_str_small_new("hello", 5)
h = ci_str_hash(s)
assert h != 0
ci_free(s)
```

### 9b: same content → same hash
```
a = ci_str_small_new("hello", 5)
b = ci_str_small_new("hello", 5)
assert ci_str_hash(a) == ci_str_hash(b)
ci_free(a); ci_free(b)
```

### 9c: different content → different hash
```
a = ci_str_small_new("hello", 5)
b = ci_str_small_new("world", 5)
assert ci_str_hash(a) != ci_str_hash(b)
ci_free(a); ci_free(b)
```

### 9d: empty small string hash — nonzero (sentinel)
```
s = ci_str_small_new("", 0)
h = ci_str_hash(s)
assert h != 0
assert h == ci_str_hash(s)              // consistent across calls
ci_free(s)
```

### 9e: matches equivalent full ci_str
```
s  = ci_str_small_new("hello", 5)
fs = ci_str_from_cstr("hello")
assert ci_str_hash(s) == ci_str_hash(fs)
ci_free(s); ci_dec(fs)
```

---

## Section 10 — eq / eq_cstr on small strings

All four combinations: small×small, small×full, full×small, full×full.

### 10a: small == small, same content → 1
```
a = ci_str_small_new("hello", 5)
b = ci_str_small_new("hello", 5)
assert ci_str_eq(a, b) == 1
ci_free(a); ci_free(b)
```

### 10b: small == small, different → 0
```
a = ci_str_small_new("hello", 5)
b = ci_str_small_new("world", 5)
assert ci_str_eq(a, b) == 0
ci_free(a); ci_free(b)
```

### 10c: small == full, same content → 1 (both directions)
```
s  = ci_str_small_new("hello", 5)
fs = ci_str_from_cstr("hello")
assert ci_str_eq(s, fs) == 1
assert ci_str_eq(fs, s) == 1
ci_free(s); ci_dec(fs)
```

### 10d: small == full, different → 0
```
s  = ci_str_small_new("hello", 5)
fs = ci_str_from_cstr("world")
assert ci_str_eq(s, fs) == 0
ci_free(s); ci_dec(fs)
```

### 10e: different lengths → 0
```
a = ci_str_small_new("hi", 2)
b = ci_str_small_new("hello", 5)
assert ci_str_eq(a, b) == 0
ci_free(a); ci_free(b)
```

### 10f: both empty → 1
```
a = ci_str_small_new("", 0)
b = ci_str_small_new("", 0)
assert ci_str_eq(a, b) == 1
ci_free(a); ci_free(b)
```

### 10g: eq_cstr match
```
s = ci_str_small_new("hello", 5)
assert ci_str_eq_cstr(s, "hello") == 1
ci_free(s)
```

### 10h: eq_cstr cstr longer → 0
```
s = ci_str_small_new("hell", 4)
assert ci_str_eq_cstr(s, "hello") == 0
ci_free(s)
```

### 10i: eq_cstr cstr shorter → 0
```
s = ci_str_small_new("hello", 5)
assert ci_str_eq_cstr(s, "hell") == 0
ci_free(s)
```

### 10j: eq_cstr empty vs "" → 1
```
s = ci_str_small_new("", 0)
assert ci_str_eq_cstr(s, "") == 1
ci_free(s)
```

---

## Section 11 — copy from small string source

### 11a: copy → new independent full ci_str
```
s    = ci_str_small_new("hello", 5)
copy = ci_str_copy(s, 0)
assert CI_IS_STR(copy)                   // always full ci_str
assert ci_str_len(copy) == 5
assert memcmp(ci_str_head(copy), "hello", 5) == 0
assert ci_str_eq(s, copy) == 1
ci_free(s); ci_dec(copy)
```

### 11b: copy with extra tail
```
s    = ci_str_small_new("hi", 2)
copy = ci_str_copy(s, 50)
assert ci_str_len(copy) == 2
assert ci_str_tail_space(copy) >= 50
ci_free(s); ci_dec(copy)
```

### 11c: copy of empty small
```
s    = ci_str_small_new("", 0)
copy = ci_str_copy(s, 0)
assert ci_str_len(copy) == 0
ci_free(s); ci_dec(copy)
```

---

## Section 12 — ensure_head / prepend upgrade

### 12a: ensure_head upgrades small to full
```
s = ci_str_small_new("hello", 5)         // 64-byte slot
assert CI_IS_STR_SMALL(s)
p = ci_str_ensure_head(s, 10)
assert p != NULL
assert !CI_IS_STR_SMALL(s)              // upgraded
// data preserved — s is now full ci_str, start points to "hello"
assert ci_str_len(s) == 5
assert memcmp(ci_str_head(s), "hello", 5) == 0
assert ci_str_head_space(s) >= 10
ci_free(s)
```

### 12b: prepend on small → upgrade + data correct
```
s = ci_str_small_new("world", 5)
assert CI_IS_STR_SMALL(s)
r = ci_str_prepend(s, "hello ", 6)
assert r == 1
assert !CI_IS_STR_SMALL(s)              // upgraded
assert ci_str_len(s) == 11
assert memcmp(ci_str_head(s), "hello world", 11) == 0
ci_free(s)
```

### 12c: after upgrade, full ci_str ops work
```
s = ci_str_small_new("test", 4)
ci_str_prepend(s, "XX", 2)              // triggers upgrade
// now it's a full ci_str — all ops should work:
ci_str_append(s, "YY", 2)
assert ci_str_len(s) == 8
assert memcmp(ci_str_head(s), "XXtestYY", 8) == 0
ci_str_compact(s)
assert ci_str_head_space(s) == 0
ci_str_rmhead(s, 2)
assert memcmp(ci_str_head(s), "testYY", 6) == 0
ci_str_rmtail(s, 2)
assert memcmp(ci_str_head(s), "test", 4) == 0
h = ci_str_hash(s)
assert h != 0
ci_str_clear(s)
assert ci_str_len(s) == 0
ci_free(s)
```

### 12d: ensure_head(0) on small — still upgrades (head space is a full-string concept)
```
s = ci_str_small_new("hi", 2)
p = ci_str_ensure_head(s, 0)
// upgrade triggered because CI_IS_STR_SMALL check comes first
// after upgrade, head_space is 0 which satisfies n=0, returns start-0=start
assert p != NULL
assert !CI_IS_STR_SMALL(s)
assert ci_str_len(s) == 2
ci_free(s)
```

---

## Section 13 — upgrade destructor

### 13a: upgraded small string frees malloc'd buffer on ci_free
```
// Just verify no crash / leak (run under valgrind)
s = ci_str_small_new("test", 4)
ci_str_prepend(s, "XX", 2)              // upgrade
ci_free(s)                               // should free the malloc'd buffer
```

### 13b: non-upgraded small string destructor is no-op
```
s = ci_str_small_new("test", 4)
ci_free(s)                               // destructor checks CI_OBJ_SMALL, skips free
```

---

## Section 14 — producer/consumer loop with small string

Verify rmhead drain + append reuse works over many cycles.
Small strings have no pointer drift (no start/end pointers), but
confirm slot capacity stays constant and data integrity holds.

```
s = ci_str_small_new("", 0)             // 64-byte slot, cap=58
initial_size = ci_str_size(s)

for i in 0..1000:
    r = ci_str_append(s, "AAAA", 4)
    assert r == 1
    n = ci_str_rmhead(s, 4)
    assert n == 4
    assert ci_str_len(s) == 0
    assert ci_str_head_space(s) == 0

assert ci_str_size(s) == initial_size    // no realloc, slot unchanged
ci_free(s)
```

---

## Section 15 — small string allocation boundaries

### 15a: first size class is 64 (32 removed from allocation chain)
```
s = ci_str_small_new("x", 1)
assert CI_IS_STR_SMALL(s)
assert tg_ptr_size(s) == 64             // smallest available pool
ci_free(s)
```

### 15b: empty string gets 64
```
s = ci_str_small_new("", 0)
assert tg_ptr_size(s) == 64
ci_free(s)
```

### 15c: max 58 bytes fits in 64-byte pool
```
s = ci_str_small_new(<58-byte buf>, 58)
assert tg_ptr_size(s) == 64
ci_free(s)
```

### 15d: 59 bytes bumps to 128-byte pool
```
s = ci_str_small_new(<59-byte buf>, 59)
assert tg_ptr_size(s) == 128
ci_free(s)
```

### 15e: max 122 bytes fits in 128
```
s = ci_str_small_new(<122-byte buf>, 122)
assert tg_ptr_size(s) == 128
ci_free(s)
```

### 15f: 123 bytes bumps to 256
```
s = ci_str_small_new(<123-byte buf>, 123)
assert tg_ptr_size(s) == 256
ci_free(s)
```

### 15g: max 250 bytes fits in 256
```
s = ci_str_small_new(<250-byte buf>, 250)
assert tg_ptr_size(s) == 256
ci_free(s)
```

### 15h: 251 bytes → NULL (exceeds all pools)
```
s = ci_str_small_new(<251-byte buf>, 251)
assert s == NULL
```

---

## Section 16 — null termination in clear (C compat)

### 16a: full ci_str clear null-terminates
```
s = ci_str_from_cstr("hello")
ci_str_clear(s)
assert ci_str_head(s)[0] == '\0'
ci_dec(s)
```

### 16b: small string clear null-terminates
```
s = ci_str_small_new("hello", 5)
ci_str_clear(s)
assert ci_str_head(s)[0] == '\0'
ci_free(s)
```
