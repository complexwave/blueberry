# Maps

## Introduction

Blueberry has two map types:

- **Map** — unordered hash map. Keys compared by pointer identity. Fast O(1) average lookups.
- **Ordered map (tree)** — sorted B-tree. Keys compared by value. Iteration is always in key order. See [Ordered Maps](#ordered-maps-trees).

The two types are drop-in interchangeable. All `map.*` namespace functions (`map.len`, `map.keys`, `map.delete`, etc.) accept either type. Index syntax (`m[k]`, `m[k] = v`) works on both. Code written against one type works with the other — swap `map.new()` for `tree.new()` to get sorted iteration, or vice versa for raw speed.

Map literals use `{}` syntax and create regular maps:

```javascript
var m = { x: 1, y: 2 }
m["x"]       // 1
m["x"] = 99  // set
```

`m[key]` and `m[key] = val` work for both map types.

**Keys use pointer identity** — string literals are interned and compare equal by pointer, so they work as keys. Mutable strings, boxed numbers, and arrays are matched by object identity only, not value. Use string literals or interned strings as keys in practice.

**`map.exists(m, key)` vs. `m[key] == null`** — a key can exist with a `null` value. `exists` distinguishes the two cases.

---

## `map` namespace

The `map` namespace functions work on both regular maps and ordered maps.

### `map.new(size?)`

Creates a new empty map. `size` pre-reserves bucket space. Default is 16.

```javascript
var m = map.new()
var m = map.new(64)
```

### `map.len(m)`

Returns the number of key-value pairs.

```javascript
map.len({ a: 1, b: 2 })   // 2
```

### `map.keys(m)`

Returns an array of all keys. Order is unspecified for regular maps; sorted for ordered maps.

```javascript
map.keys({ a: 1, b: 2 })   // ["a", "b"] (any order)
```

### `map.values(m)`

Returns an array of all values. Order matches `map.keys`.

```javascript
map.values({ a: 1, b: 2 })   // [1, 2] (matching key order)
```

### `map.delete(m, key)`

Removes `key` from the map. Returns `true` if the key existed, `false` otherwise.

```javascript
var m = { a: 1, b: 2 }
map.delete(m, "a")   // true
map.delete(m, "z")   // false
```

### `map.exists(m, key)`

Returns `true` if `key` is present, even if its value is `null`. Use this to distinguish a missing key from a key set to `null`.

```javascript
var m = { a: null }
m["a"] == null        // true  — but is it missing?
map.exists(m, "a")    // true  — no, it's there
map.exists(m, "b")    // false — this one is missing
```

### `map.size(m, newsize?)`

For regular maps: with no argument returns the allocated bucket count. With an integer argument, ensures at least that many buckets are allocated and returns the new bucket count.

For ordered maps: returns the entry count (setter is ignored).

```javascript
var m = map.new()
map.size(m, 1024)   // pre-expand hash table
```

### `map._merge(dst, src)`

Shallow-merges all entries from `src` into `dst`. Keys in `src` overwrite matching keys in `dst`. Both must be regular maps (not ordered maps). Returns `dst`.

```javascript
var a = { x: 1, y: 2 }
var b = { y: 99, z: 3 }
map._merge(a, b)   // a is now { x: 1, y: 99, z: 3 }
```

---

## Ordered maps (trees)

An ordered map keeps keys sorted and supports iteration in key order. Created with `tree.new()`.

Keys are compared by value: strings sort lexicographically, numbers sort numerically. Mixed key types follow the VM's comparison rules.

```javascript
var t = tree.new()
t["banana"] = 2
t["apple"]  = 1
t["cherry"] = 3

map.keys(t)     // ["apple", "banana", "cherry"] — always sorted
```

### `tree.new()`

Creates a new empty ordered map.

```javascript
var t = tree.new()
```

---

## Ordered map methods

Ordered maps have object methods in addition to accepting `map.*` namespace calls.

| Method | Description |
|--------|-------------|
| `.set(key, val)` | Insert or update a key |
| `.get(key)` | Retrieve value; `null` if not found |
| `.delete(key)` | Remove key; returns `true` if it existed |
| `.exists(key)` | True if key is present (distinguishes null from missing) |
| `.len()` / `.size()` | Number of entries |
| `.keys()` | Array of keys in sorted order |
| `.values()` | Array of values in key-sorted order |
| `.clear()` | Remove all entries |

Index syntax (`t[k]`, `t[k] = v`) also works and calls `.get`/`.set`.

```javascript
var t = tree.new()
t.set("b", 2)
t.set("a", 1)
t.get("a")       // 1
t["c"] = 3
t.len()          // 3
t.keys()         // ["a", "b", "c"]
t.exists("a")    // true
t.delete("a")    // true
t.exists("a")    // false
```
