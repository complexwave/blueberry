# Design

## Philosophy

**Be not annoying.**
Avoid semantic quirks or experimental syntax paradigms. Provide control of the runtime, enough stdlib, and be predictable. Be flexible, be hackable from compiler to VM level.

**Foundations included.**
Not full batteries included, but don't force the user to reinvent deep copy, directory listing, or piping data to external processes. The annoying things that live in `utils/` of any embedded/backend project should be in the language.

**You already know it.**
If you know JS and Lua you already know the syntax. No inventions.

**Performance focused.**
Aim to match Lua 5.4 and LuaJIT interpreter-mode performance.

**C glue, exposed internals.**
Dumb compiler, clever VM. Allow control of things like preallocating string/array/map sizes. Explicit copy for concat.

**Async native.**
Integrated async runtime in the VM itself. Builtin coroutine scheduler. `io_uring`-backed transparent syscalls planned.

## Implementation

- Tailcalling interpreter with `musttail` and clang `preserve_none` calling convention
- Custom arena allocator using `mmap` to embed type tags into valid pointer addresses (6 bits, no untagging needed)
- Separate `[]` (0-indexed), `{}`, and tree structures
- Mutable strings with correct map lookup behavior
- PreIndex-Map: Robin Hood based map matching SwissTable on small maps, portable without SIMD
- Default integers up to `size_t - 1`, with transparent boxing for doubles and int128
- Cmatcher PEG parser combinator library
- Multiple-pass compiler to bytecode with simple IR-like objects

## Performance

| Benchmark | lua5.4 | luajit | luajit-nojit | blueberry |
|-----------|--------|--------|--------------|-----------|
| binarytrees (12) | 0.056s | 0.028s | 0.037s | 0.072s |
| merkletrees (11) | 0.053s | 0.032s | 0.035s | 0.085s |
| nsieve (7) | 0.104s | 0.030s | 0.072s | 0.077s |
| maps (1M) | 0.076s | 0.005s | 0.081s | 0.100s |
