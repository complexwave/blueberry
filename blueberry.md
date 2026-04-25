# BlueBerry VM — Fast Dispatch Notes

## What was done

Replaced the old switch-based dispatch in `bb_vm_execute` with a precached
`{fnptr, ctx}` array. Each bytecode instruction is converted at function entry
into a `bb_cached_op` with a resolved function pointer and repacked operands.

### ctx layout (b0 stripped, fnptr encodes the op)

- **RRR:** `[d:8][a:8][b:8][pad:8]` — three register indices
- **RI:** `[d:8][a:8][imm16:16]` — unified format for both RRI8 and RI16 origins.
  RRI8 widens imm8→imm16. Native RI16 ops duplicate d into the a slot (ignored).
- **VAR:** not implemented, padded with nops

### Execute loop

```c
while (1) {
    bb_cached_op *op = c->ops_pc;
    op->fn(c, op->ctx);
}
```

Each handler advances `c->ops_pc++`. Jump handlers set `c->ops_pc = c->ops_base + target`.
Exit sentinel calls `exit(0)`.

### Macros

- `VM_FAST_RRR(label, impl)` — generates `__vmop_<label>_rrr`
- `VM_FAST_RI(label, impl)` — generates `__vmop_<label>_ri`
- `FT_RRR`, `FT_RI`, `FT_RI16` — register handlers in `bb_fast_table[256]`
  indexed by raw first byte `(subtype:2 | opnum:6)`
- `BB_ST_RRR`, `BB_ST_RRI8`, `BB_ST_RI16`, `BB_ST_VAR` — subtype bit constants

### Coro additions

- `ci_ptr *fast_stack` — cached stack pointer
- `bb_cached_op *ops_pc` — current instruction pointer
- `bb_cached_op *ops_base` — base of cached ops array (for jumps)

## Benchmark results (1B iterations, loop.ci)

| Runtime       | Time  |
|---------------|-------|
| Node.js (JIT) | 0.27s |
| LuaJIT (luvit)| 0.49s |
| Lua           | 5.9s  |
| BlueBerry     | 6.9s  |
| Python        | 31.4s |

Old dispatch was ~14.9s for 1B. Precache halved it. ~16% behind Lua.

## What is broken / unfinished

1. **RETURN is broken** — `__vmop_exit` calls `exit(0)`. No proper return,
   no frame pop, no multi-function support. Need to restore old return logic
   with ops_pc/ops_base save/restore per frame.

2. **CALL is broken** — same reason. Need to save caller's ops_pc/ops_base
   in the frame, build cached ops for callee, restore on return.

3. **VAR ops not dispatched** — NEWMAP, NEWARRAY, HASHACCESS(var), LOADNULL,
   RETURN, CALL all padded as nops. ctx is uint32_t, can't hold a pointer for
   VAR ops. Need to widen ctx to `size_t`/`uintptr_t` to hold code pointers.

4. **ci_inc/ci_dec noop hack** — `#undef ci_inc` / `#undef ci_dec` placed
   before `bb_vm_execute` but AFTER the macro-generated wrappers, so the
   handlers still have real ci_inc/ci_dec. Doesn't matter for perf (packed
   ints bail immediately) but the hack is ineffective. Remove it.

5. **BB_CBC_ONLY guard** — partially added for clang-only .cbc builds
   (`#ifndef BB_CBC_ONLY` around encoder.c include and compile.c include).
   Not finished — `b_malloc`, `b_op_names` etc still unresolved. The cma
   parser library uses GCC-specific `static` compound literals that clang
   rejects, blocking a full clang build.

6. **Makefile** — `CC=gcc` with c11. Intended clang target not added yet.
   `bench.sh` and loop scripts (loop.js, loop.py, loop.lua) in project root.

## Future directions discussed

- **Wider opcode format**: `2 * size_t` (16 bytes on 64-bit) per op.
  fnptr + ctx as native words. Bytecode IS the cached array, no conversion.
  Use gzip for .cbc storage instead of clever packing.

- **musttail calls**: each handler ends with
  `__attribute__((musttail)) return next->fn(c, next->ctx);`
  Eliminates call/ret overhead, register spills. Needs clang.

- **Computed gotos**: proven faster than Lua in prior prototype, but causes
  register spills across the giant switch body.

- **ASM findings**: each indirect call currently pushes/pops 5 callee-saved
  regs (rbx, rbp, r12-r15). That's the main overhead vs computed gotos.
  `endbr64` (CET) also present on every handler entry.
