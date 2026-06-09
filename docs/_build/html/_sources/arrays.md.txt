# Arrays

## Introduction

Arrays are dynamic, ordered sequences of values. Elements can be any type — numbers, strings, other arrays, objects, `null`.

Arrays are implemented as a circular buffer with amortized O(1) push and shift. Capacity is managed automatically; use `.size()` to preallocate when the final size is known upfront.

Index syntax reads and writes elements:

```javascript
var a = [1, 2, 3]
a[0]        // 1
a[-1]       // 3  (negative indices wrap from the end)
a[0] = 99   // set
```

`a[i]` calls `.get(i)`, `a[i] = v` calls `.set(i, v)`.

**Find and contains use pointer equality** — boxed numbers and mutable strings are matched by identity, not value. Use a manual loop for value-based search on those types.

---

## `array` namespace

### `array.new(size?)`

Creates a new empty array with preallocated capacity. The array starts empty; capacity is reserved immediately to avoid reallocation during early pushes. Default initial capacity is 16.

```javascript
var a = array.new()
var a = array.new(256)   // pre-reserve 256 slots
```

---

## Object methods

| Method | Description |
|--------|-------------|
| `.len()` | Number of elements |
| `.size()` | Allocated capacity |
| `.size(n)` | Set length to `n` — grows capacity or pops elements; returns new capacity |
| `.push(val)` | Append to end |
| `.pop()` | Remove and return last element |
| `.unshift(val)` | Prepend to front |
| `.shift()` | Remove and return first element |
| `.get(idx)` | Element at index; negative wraps; `null` if out of bounds |
| `.set(idx, val)` | Set element at index; extends array if beyond end; negative wraps |
| `.find(val, start?)` | Index of first match from `start`, or `false` if not found |
| `.contains(val)` | True if value is present |
| `.slice(from, to?)` | New copy of subarray `[from, to)` |
| `.copy()` | New independent copy |
| `.clear()` | Remove all elements |
| `.reverse()` | Reverse in place |
| `.merge(arr, ...)` | Append all elements from one or more arrays; returns self |
| `.splice(start, count, ...)` | Remove `count` elements at `start`, insert varargs in their place; returns self |
| `.splice_arr(start, count, arr)` | Same as `.splice` but inserts come from an array |

---

### `.len()`

Returns the number of elements.

```javascript
[1, 2, 3].len()   // 3
```

### `.size(n?)`

With no argument, returns the allocated capacity (always `>= .len()`).

With an integer argument:
- If `n > len`: ensures at least `n` slots are allocated (does not change length).
- If `n < len`: pops elements until `len == n`.

Returns the new allocated capacity.

```javascript
var a = array.new()
a.size(1024)   // reserve 1024 slots upfront
```

---

### `.push(val)` / `.pop()`

`.push(val)` appends to the end. `.pop()` removes and returns the last element, or `null` if empty.

```javascript
var a = [1, 2, 3]
a.push(4)   // [1, 2, 3, 4]
a.pop()     // 4 — a is now [1, 2, 3]
```

### `.unshift(val)` / `.shift()`

`.unshift(val)` prepends to the front. `.shift()` removes and returns the first element, or `null` if empty. Both are O(1) thanks to the circular buffer.

```javascript
var a = [1, 2, 3]
a.unshift(0)   // [0, 1, 2, 3]
a.shift()      // 0 — a is now [1, 2, 3]
```

---

### `.get(idx)` / `.set(idx, val)`

`.get(idx)` returns the element at `idx`. Negative indices wrap from the end. Returns `null` if out of bounds.

`.set(idx, val)` sets the element at `idx`. Negative indices wrap. If `idx >= len`, the array is extended with `null` slots up to that index.

```javascript
var a = [10, 20, 30]
a.get(1)     // 20
a.get(-1)    // 30
a.set(1, 99) // [10, 99, 30]
a.set(5, 1)  // [10, 99, 30, null, null, 1]
```

---

### `.find(val, start?)`

Returns the index of the first occurrence of `val` at or after `start` (default 0). Returns `false` if not found. Uses pointer equality.

```javascript
var a = [10, 20, 30, 20]
a.find(20)     // 1
a.find(20, 2)  // 3
a.find(99)     // false
```

### `.contains(val)`

Returns `true` if `val` is present. Uses pointer equality.

```javascript
[1, 2, 3].contains(2)   // true
[1, 2, 3].contains(9)   // false
```

---

### `.slice(from, to?)`

Returns a new independent array containing elements `[from, to)`. Negative indices wrap. If `to` is omitted, slices to the end.

```javascript
var a = [1, 2, 3, 4, 5]
a.slice(1, 3)   // [2, 3]
a.slice(2)      // [3, 4, 5]
a.slice(-2)     // [4, 5]
```

### `.copy()`

Returns a new independent copy of the array.

```javascript
var b = a.copy()
b.push(99)   // does not affect a
```

---

### `.clear()`

Removes all elements. Capacity is retained.

```javascript
var a = [1, 2, 3]
a.clear()   // a is now []
```

### `.reverse()`

Reverses the array in place.

```javascript
var a = [1, 2, 3]
a.reverse()   // [3, 2, 1]
```

---

### `.merge(arr, ...)`

Appends all elements from one or more arrays to self. Returns self.

```javascript
var a = [1, 2]
a.merge([3, 4], [5, 6])   // [1, 2, 3, 4, 5, 6]
```

---

### `.splice(start, count, ...inserts)`

Removes `count` elements starting at `start`, then inserts any additional arguments in their place. Returns self.

`start` is clamped to `[0, len]`. `count` is clamped so it doesn't remove past the end.

```javascript
var a = [1, 2, 3, 4, 5]
a.splice(1, 2)          // remove 2 at index 1 → [1, 4, 5]
a.splice(1, 0, 9, 8)    // insert at index 1, delete 0 → [1, 9, 8, 4, 5]
a.splice(1, 2, 10, 11)  // replace 2 with 2 → [1, 10, 11, 4, 5]
```

### `.splice_arr(start, count, insertArray)`

Same as `.splice` but inserts come from an array instead of varargs. Useful when the insert list is dynamic.

```javascript
var inserts = [9, 8, 7]
a.splice_arr(1, 2, inserts)
```
