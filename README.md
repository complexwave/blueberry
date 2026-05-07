# Blueberry lang

A scripting language written in C.

Goal is to create flexible, performant and pleasant to use scripting language targeted at use as high performance backend, system/network programming and general purpose use.

Inspired by Lua and JS, but I try to avoid pain points of those languages.

Born from working a lot with async code and LuaJIT, and being tired of Lua's nonexistent stdlib and arbitrary quirks. I also work with embedded Linux platforms and bringing Python or other bloated things like Node.js often just doesn't fit into flash.

The good system programming scripting niche is underserved.

- Python is too bloated, version hell
- Node.js is bloated and JS semantics is a legacy mess. Other JS runtimes too minimal, like Lua
- Go has bloated runtime in binaries and is static language
- C is unnecessary and slow/buggy to write JSON shuffling and bash command calling.
- Lua is good but has weird quirks like table-arrays with 1-based indexes. Stdlib is nonexistent and production projects have 30% of codebase in `utils/` and `libs/`

This project aims to provide a quality small, but not minimalistic, performant and flexible language for these tasks, that just does its thing and gets out of the way. 
Not being too opinionated on how you should code, not introducing novel type systems, but doing the already existing scripting paradigm BETTER and fast.

Blueberry is a berry with mild neutral taste. It just is, and it's tasty by being neutral.

---

## Performance

VM is performant in basic dispatch thanks to tailcalling interpreter with `musttail` and clang-specific `preserve_none` calling convention, combined with opcode parse caching.


| Benchmark | lua5.4 | luajit | luajit-nojit | blueberry |
|-----------|--------|--------|--------------|-----------|
| binarytrees (12) | 0.056s | 0.028s | 0.037s | 0.072s |
| merkletrees (11) | 0.053s | 0.032s | 0.035s | 0.085s |
| nsieve (7) | 0.104s | 0.030s | 0.072s | 0.077s |
| maps (1M) | 0.076s | 0.005s | 0.081s | 0.100s |


Its has some dispatch overhead still and lua can be matched with some microoptimizations later
There seem to be overhead in hashmap opcode while map itself raw is fast, current opcode is overcomplicated.
I have chained superinstruction for evaluating multiple keys in one instruction, but it seems like better to make separate opcodes

---

## Syntax

```javascript
var name = "Blueberry"
var greeting = "Hello from ".copy().append(name).append("!")
print(greeting)

// arrays are 0-indexed
var nums = [10, 20, 30]
nums.push(40)
for i, v in nums {
    print(i, v)
}

// maps
var config = { host: "localhost", port: 8080 }
config.debug = true
print(config.host, config.port, config.debug)

// prototype OOP
Vec = {}
fn Vec.new(x, y) {
    var v = {}

    proto.set(v, self)

    v.x = x
    v.y = y

    return v
}
fn Vec.len2() {
    return self.x * self.x + self.y * self.y
}
fn Vec.add(other) {
    return Vec.new(self.x + other.x, self.y + other.y)
}

var a = Vec.new(3, 4)
var b = Vec.new(1, 2)
var c = a.add(b)
print("vec:", c.x, c.y, "len2:", c.len2())
```

---

## Philosophy

**Be not annoying.**
Avoid semantics weird quirks or experimental syntax paradigms. Provide control of runtime, enough stdlib and be predictable. If can avoid limiting user, do so. Be flexible, be hackable from compiler to VM level.

**Foundations included.**
Not full batteries included, but don't force user to reinvent deep copy, directory listing or way to pipe data in and out of an external process. All the annoying things that are just tedious to remake each time and live in `utils/` folder of any embedded/backend software should be in the language.

**You already know it.**
If you know JS and Lua you already know the syntax. No inventions. Some new operators, but not much. See basic docs for stdlib and you're good to go.

**Performance focused.**
Aim is to match Lua 5.4 and LuaJIT (in interpreter mode) performance level, and those are some of the fastest existing interpreters.

**C glue, exposed internals.**
Language doesn't try to hide what it is, or the computer running it behind abstraction. Dumb compiler, clever VM. It should be obvious what the VM does from code, no clever syntax or high abstraction concepts. Allow control of things like preallocating string/array/map sizes. Explicit copy for `concat()`.

**Async native.**
Integrate async runtime in VM itself, not bolted on libuv. Builtin coroutine scheduler preventing reentrancy bugs (yield across C boundary in Lua). Everything that accepts a callback can also accept a coroutine and resume it. `io_uring` backed transparent syscalls planned, including uring memory pool backed strings and streams, so things like proxying between sockets can be zero copy.

---

## Design decisions

- Dynamic language with Lua-like semantics. No auto typecast (only for ints like in C expressions) but auto vars
- Prototype based OOP with optional "semi-typed" system of registered named prototypes
- Tailcalling interpreter with `musttail` and clang-specific `preserve_none` calling convention
- Custom arena allocator that uses `mmap` to embed type information into valid pointer addresses, allowing to store 6 bits of tags in a valid pointer without need for any untagging or memory access
- Separate `[]` (0-indexed), `{}`, and included tree structure. No Lua 1-based table-array nonsense
- Mutable strings with transparent correct behaviour when looking up in maps
- Fast map implementation is basis for objects. Novel preindex-map: variant of Robin Hood based map, matching Google SwissTable performance on small (10-4000 entries) maps and being competitive (little slower) at higher loads, portable without SIMD dependence
- Default integers are ints up to `size_t - 1` size, with transparently boxed support of double floating point and int128 integers
- Planned native support of coroutine scheduling and `io_uring` based runtime. Async runtime in language design, not a bolted on afterthought
- Cmatcher PEG parser combinator library similar to LPeg Lua library, with nice C API and in-language binding
- Multiple pass compiler to bytecode with own simple IR-like objects
- Planned to rewrite compiler in language itself

---

## Project state

This is a work in progress second prototype.

I wrote a similar (not published) scripting language 5 years ago, but lost interest because of the amount of work involved.

AI allowed to continue on this project, rewriting it from scratch based on previous knowledge and ideas I accumulated over years. Now I can focus on high level architecture, debugging and refinement, while leaving boilerplate for AI — which is especially useful when generating things like parsers or opcode tables, or doing refactoring.

Basic VM functionality and compiler work. Basic stdlib is lacking and needs a lot of refinement. If you are interested look at `tests/` and `scripts/` folders for a pile of examples. Documentation is currently nonexistent unfortunately.

Async runtime not properly implemented yet, although there is a basic coroutine sketch.

**Current focus:** finishing stdlib of all basic types and syntax in general.

**Planned:**
- Async runtime with `io_uring` syscalls transparently
- FFI for calling external libs, syscalls, ioctls
- Simple non-tracing macro based JIT

---

## Building

Currently only tested on Intel x86-64.

You must have clang-23 or newer (gcc fallback later).

```bash
make blueberry
./blueberry helloworld.ci
```

Run tests (this is also currently the only source of syntax reference):

```bash
./tests.sh
```

---

## License

GPL 3

As single author I'm interested in developing this project, with potential relicense into open source with less restrictive license or commercial license.

Contact me if interested in cooperation.
