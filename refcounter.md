# Blueberry VM — Refcounting Guide

Checklist for adding correct `ci_inc`/`ci_dec` calls across the VM.
Built from analysis of the codebase with `TGMEMLIB_TRACKING` + ASan instrumentation.

---

## Core rules

1. **`ci_new` returns rc=1.** The caller owns it. No `ci_inc` needed at creation.
2. **`ci_inc` before storing** a value into a second location (stack slot, map, array, field).
3. **`ci_dec` before overwriting** a slot that already holds a refcounted value.
4. **`ci_dec` when a value leaves scope** (function return, scope exit, loop iteration).
5. **Never `ci_dec` below 1 on a live value.** Pop/shift return ownership to caller — caller decides when to dec.
6. **`ci_nocnt` = immortal.** Interned strings, prototypes, global closures. Never freed.
7. **Ints, bools, null are not refcounted.** `ci_inc`/`ci_dec` on them are no-ops (CI_IS_REFCOUNTABLE check).

---

## Tracking tools

```
make tracking     # -DTGMEMLIB_TRACKING, refcounting ON
make asan         # ASan + tracking, -O0, no preserve_none/musttail
```

Script API:
```
gc.trace_start()   // snapshot all live objects
// ... user code ...
gc.trace_print()   // show only NEW objects since snapshot, with refcounts
```

Diagnosis:
- **rc=1, never freed** → missing `ci_dec` at scope exit / overwrite
- **rc=N where N>1** → too many `ci_inc` or missing `ci_dec` on extra references
- **rc=0 NOT FREED** → `ci_dec` underflow — something dec'd without owning
- **ASan use-after-poison** → use-after-free: code touches object after `ci_dec` freed it

---

## Return value calling convention

**Every return value from a C function is an owned reference.**
The rc already accounts for the return — the call site either stores it (taking
ownership) or dec's it (discarding).

### Why this convention

The hard problem: `arr.pop()` removes an element. If pop dec's the element,
rc may hit 0 and the object is freed before the caller can use it. If pop
doesn't dec, and the caller discards the result, it leaks.

Solution: pop doesn't touch rc. The element's reference transfers from the
array to the return value. The VM call wrapper is responsible for dec'ing
if the return is discarded.

### Macros for bb_cfn (simple calling convention)

Use these in all `bb_cfn`-typed C functions instead of bare `return`:

```c
BB_RETURN(val)        // val lives elsewhere — ci_inc's it, then returns as ci_ptr
BB_RETURN_NOINC(val)  // val is already an owned ref — returns as ci_ptr, no inc
```

Plain `return` is correct for scalars: `CI_PACKINT`, `CI_BOOL`, `NULL`.
`ci_inc`/`ci_dec` are no-ops on non-refcountable values, but avoid the macro
for clarity.

| Return case | Macro | Reason |
|---|---|---|
| New object (`map.new()`, `str.copy()`) | `BB_RETURN_NOINC` | `ci_new` rc=1 IS the return ref |
| Self-return (`str.append()`) | `BB_RETURN` | self has other refs; return is an extra one |
| Existing value (`map.get(k)`) | `BB_RETURN` | val lives in container; return is an extra ref |
| Container remove (`arr.pop()`) | `BB_RETURN_NOINC` | element's ref transfers from container to return |
| Scalar (`CI_PACKINT`, `CI_BOOL`, `NULL`) | plain `return` | not refcounted |

These macros are defined in `blueberry_vm/api.c` (included before all lib/proto files).

### What the callee does

| Return case | Callee action | Reason |
|---|---|---|
| New object (`map.new()`, `str.copy()`) | nothing | `ci_new` gives rc=1, that IS the return ref |
| Self-return (`str.append()`) | `ci_inc(self)` | self already has refs, return is an extra one |
| Existing value (`map.get(k)`) | `ci_inc(val)` | val lives in map, return is an extra ref |
| Container remove (`arr.pop()`) | nothing | element leaves container, its ref becomes return ref |

### What the VM call wrapper does

| Call site | VM action |
|---|---|
| Stored: `var x = f()` | `ci_dec(old_x)`, store return. No inc. |
| Discarded: `f()` as statement | `ci_dec(return_val)` |
| Chained: `a.f().g()` | return of `f()` is temporary; dec after `g()` consumes it |
| Chained self-mutation: `s.append("a").append("b")` | each append inc's self; intermediates dec'd by VM |

### Chaining walkthrough: `s.append("a").append("b")`

```
s is in register, rc=1
1. call s.append("a"):
   - callee does ci_inc(self) → s rc=2
   - returns s (owned ref, rc=2)
2. return value is temporary, becomes receiver for .append("b")
3. call .append("b") on temporary:
   - callee does ci_inc(self) → s rc=3
   - returns s (owned ref, rc=3)
4. VM decs intermediate from step 1 → rc=2
5. VM stores final return in register:
   - ci_dec(old register value = s at rc=2) → rc=1
   - store s → register holds s at rc=1
```

Net result: s stays at rc=1. All intermediate refs balanced.

### Pop walkthrough: `var x = arr.pop()`

```
element in array at rc=1 (array is only owner)
1. pop removes element from array storage (no dec)
   - element rc stays 1, ownership transfers to return value
2. VM stores return in register x:
   - ci_dec(old x) → releases whatever x held before
   - x = element, rc=1 — caller now owns it
3. later, when x goes out of scope:
   - ci_dec(x) → rc=0 → freed
```

### Discarded pop: `arr.pop()` as statement

```
element in array at rc=1
1. pop removes element, returns it (rc=1)
2. VM discards return → ci_dec(return_val) → rc=0 → freed
```

No leak, no underflow.

### Summary

C function authors follow one rule: **if the returned value has refs elsewhere,
inc it before returning. If it's new or being removed from its only container,
just return it.** The VM handles everything else.

---

## Opcode patterns

### NEWMAP / NEWARRAY (advanced_opcodes.c)

Already correct pattern:
```c
ci_map *new_map = ci_map_new(16);        // rc=1, we own it
for each value:
    ci_inc(val);                          // map gets a ref to val
    ci_map_put(new_map, key, val);
ci_dec(stack[dst_reg]);                   // dec old value in dst
stack[dst_reg] = (ci_ptr)new_map;         // transfer ownership, no inc needed
```

### MOVETO / MOVEFROM (advanced_opcodes.c)

Already correct pattern:
```c
ci_inc(val);              // new slot gets a ref
ci_dec(stack[dst]);       // old value in dst loses a ref
stack[dst] = val;
```

### Local variable assignment (generic STORE to register)

**Anywhere `stack[reg] = value` happens, the old value must be dec'd first.**

Pattern:
```c
ci_ptr old = stack[reg];
stack[reg] = new_val;     // new_val already owned (rc=1 from ci_new, or ci_inc'd)
ci_dec(old);              // release old
```

If `new_val` comes from another slot and will stay there too: `ci_inc(new_val)` before storing.

### Scope exit / function return

**All local registers that hold refcounted objects must be dec'd on:**
- Function return (RETURN opcode)
- Scope exit (end of block with locals)
- Exception / error path (bb_coro_error longjmp)

Missing dec's here are the #1 source of leaks. The compiler should emit cleanup
for all live registers before RETURN.

### HASHACCESS chain (a.b.c.d)

Each intermediate lookup result is temporary. If the chain stores intermediates
in registers, those registers need dec on overwrite. Final result stored in dst
needs dec of old dst value.

---

## Container operations

### ci_map_put — replace existing key

**BUG:** `ci_map_put` on existing key overwrites `kv->val` without dec'ing old value.
```c
if (kv) {
    // MISSING: ci_dec(kv->val) before overwrite
    kv->val = val;
    return 1;
}
```
Caller must dec old value, or `ci_map_put` itself should handle it.
Decision: either make `ci_map_put` refcount-aware or document that caller must
`ci_dec(ci_map_get(m, key))` before put. Prefer making `ci_map_put` handle it
since callers will forget.

### ci_map_delete / ci_map_remove

**BUG:** Deletes key+value from map without dec'ing either.
```c
static inline void ci_map_delete_kv(ci_map *m, ci_map_kv *kv) {
    // backward shift...
    kvs[pos].key = NULL;
    kvs[pos].val = NULL;
    // MISSING: ci_dec(old_key), ci_dec(old_val)
}
```

### ci_map destructor

When a map is freed (rc reaches 0), its destructor must dec all live key+value pairs.
Check that `ci_map_destructor` walks all KVs and dec's them.

### ci_arr_push

Caller must `ci_inc(val)` before push (array gets a reference).
Already done correctly in NEWARRAY opcode.

### ci_arr_pop / ci_arr_shift

Returns element to caller. The element's ref was held by the array.
**Ownership transfers to caller** — caller is now responsible for dec.

**TRAP:** If caller ignores the return value (pops and discards), the value leaks.
Common pattern should be:
```c
ci_ptr val = ci_arr_pop(arr);
// use val...
ci_dec(val);    // when done
```

**TRAP:** If refcounting is naively applied, a pop'd value at rc=1 (only ref is in array)
would be freed by the pop. But pop doesn't dec — it just removes from array.
This is correct: pop transfers ownership, doesn't release.

### ci_arr_set (index assignment)

Old value at that index must be dec'd, new value must be inc'd:
```c
ci_ptr old = ci_arr_index(arr, i);
ci_inc(new_val);
ci_arr_set(arr, i, new_val);
ci_dec(old);
```

### ci_array destructor

When array is freed, destructor must dec all live elements.

---

## Container destructor checklist

| Type | Destructor must dec | Status |
|---|---|---|
| ci_map | all kv->key + kv->val | CHECK |
| ci_array | all elements [0..length) | CHECK |
| ci_str | nothing (no refs) | OK |
| ci_str_slice | ci_dec(parent) | CHECK |
| bb_closure | ci_dec(upvalues) if any | CHECK |
| bb_cma_op | ci_dec(children array) | CHECK |

---

## Safe patterns

### "Create and store" (no inc needed)
```c
ci_map *m = ci_map_new(16);     // rc=1
ci_dec(stack[dst]);
stack[dst] = (ci_ptr)m;          // transfer ownership
```

### "Copy reference" (inc needed)
```c
ci_ptr val = stack[src];
ci_inc(val);
ci_dec(stack[dst]);
stack[dst] = val;
```

### "Temporary use" (no inc/dec if not stored)
```c
ci_ptr val = ci_map_get(m, key);  // borrowed ref, don't dec
// use val, don't store it anywhere new
```

### "Return from native function"
```c
ci_str *s = ci_str_new(32);     // rc=1
// fill s...
return (ci_ptr)s;                // transfer ownership to VM stack
// VM is responsible for dec'ing when register is overwritten
```

---

## Priority order for fixing

1. **Scope exit / function return** — dec all live locals (compiler change)
2. **Variable reassignment** — dec old value when register is overwritten (compiler change)  
3. **ci_map_put replace** — dec old value on key collision
4. **ci_map_delete/remove** — dec removed key+value
5. **Container destructors** — dec all contents on free
6. **Array set/replace** — dec old element
7. **Loop variable** — dec on each iteration when loop var is overwritten

---

## Project structure — relevant files

```
blueberry.c                  — unity build, #includes everything in order
blueberry.h                  — public types: bb_cfn, bb_cfn_var, bb_var_ret, bb_coro_arg, ci_ptr_arg
ciobj.h                      — ci_ptr tag system, CI_IS_*, CI_PACKINT, CI_BOOL, ci_inc, ci_dec
blueberry_vm/api.c           — BB_RETURN, BB_RETURN_NOINC, BB_VAR_PUSH_RET_INC/NOINC, BB_VAR_RETURN
                               included at line 310 of blueberry.c, before all lib/proto files
blueberry_vm/advanced_opcodes.c — CALL, HASHACCESS, ARRACCESS, MAPACCESS, MOVETO/FROM, ITERSTEP
blueberry_vm/opcodes.c       — RRR dispatch table, op_fn table
blueberry_vm/proto/string.c  — string prototype methods
blueberry_vm/proto/array.c   — array prototype methods
blueberry_vm/proto/number.c  — number prototype methods  (NOT YET AUDITED)
blueberry_vm/lib/map.c       — map.new, map.keys, map.values, etc.
blueberry_vm/lib/math.c      — math.sin, math.max, math.clamp, etc.
blueberry_vm/lib/string.c    — string namespace (NOT YET AUDITED)
blueberry_vm/lib/array.c     — array namespace (NOT YET AUDITED)
blueberry_vm/lib/io.c        — io namespace (NOT YET AUDITED)
blueberry_vm/lib/proto.c     — proto namespace (NOT YET AUDITED)
blueberry_vm/lib/cma.c       — cmatcher PEG library (BB_VAR macros fixed, deeper audit pending)
blueberry_vm/lib/gc.c        — gc.print_refcnt, gc.trace_start/print
tgmemlib/tgmemlib.c          — arena allocator, TGMEMLIB_TRACKING for rc tracing
```

---

## Current work status

**What we are doing:** Auditing all native C functions (`bb_cfn` and `bb_cfn_var`) to use the
correct return macros, and fixing VM opcodes that return values from containers without `ci_inc`.

**Why:** The VM does not inc return values from C functions on store — the callee is responsible.
Functions that returned existing refs (self, map/array values) without inc caused the VM to hold
an unowned reference, leading to use-after-free when the rc hit 0 prematurely.

**How:** Use `BB_RETURN` / `BB_RETURN_NOINC` in `bb_cfn` functions, and `BB_VAR_PUSH_RET_INC` /
`BB_VAR_PUSH_RET_NOINC` in `bb_cfn_var` functions. Plain `return` for scalars only.
Defined in `blueberry_vm/api.c`.

**Files audited so far:**
- `blueberry_vm/api.c` — macros defined here ✓
- `blueberry_vm/advanced_opcodes.c` — HASHACCESS, ARRACCESS, MAPACCESS, METHODBIND fixed ✓
- `blueberry_vm/lib/map.c` ✓
- `blueberry_vm/lib/math.c` ✓ (BB_MATH_F1/F2 use plain return for new objects — acceptable)
- `blueberry_vm/proto/string.c` ✓
- `blueberry_vm/proto/array.c` ✓

**Files not yet audited:**
- `blueberry_vm/proto/number.c`
- `blueberry_vm/lib/string.c`
- `blueberry_vm/lib/array.c`
- `blueberry_vm/lib/io.c`
- `blueberry_vm/lib/proto.c`
- `blueberry_vm/lib/cma.c` (BB_VAR macros renamed, deeper return audit pending)
- `blueberry_vm/lib/gc.c`
- `blueberry_vm/opcodes.c`

**Known bugs not yet fixed (see priority list below):**
- `ci_map_put` on existing key: doesn't dec old value before overwrite
- `ci_map_delete`: doesn't dec removed key+value
- Container destructors (map, array, closure): dec of contents on free not verified
- `bb_arr_merge` fastpath: doesn't ci_inc copied elements (marked TODO in code)
- `HASHSTORE` opcode: incs val on store but doesn't dec old map value on key collision
- Scope exit / function return: compiler doesn't emit dec for live locals yet

---

## Testing strategy

1. Run `alloctest.ci` with `make tracking` — check delta is 0 for simple cases
2. Write targeted test scripts:
   - `var a = {}; a = {}` — reassignment dec
   - `function f() { var x = {}; }; f()` — scope exit dec
   - `var m = {}; m.key = "a"; m.key = "b"` — map replace dec
   - `var a = [1,2,3]; a.pop()` — pop ownership
3. Run with `make asan` — any use-after-free means dec'd too early
4. `gc.trace_start()` before test, `gc.trace_print()` after — new objects should be 0
