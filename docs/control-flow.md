# Control Flow

Condition expressions do not require parentheses, but accept them.

```javascript
if x > 0 { }
if (x > 0) { }   // same
```

## if / else

```javascript
if x > 0 {
    print("positive")
} else if x < 0 {
    print("negative")
} else {
    print("zero")
}
```

### Truthy / falsy

`false`, `null`, and `0` are falsy. Everything else is truthy — including empty strings, empty arrays, and empty maps.

```javascript
if 0     { }   // skipped
if null  { }   // skipped
if false { }   // skipped

if ""   { }    // runs — empty string is truthy
if []   { }    // runs
if {}   { }    // runs
```

---

## while

```javascript
var x = 0
while x < 10 {
    x = x + 1
}
```

`break` exits the loop. `next` skips to the next iteration (like `continue`).

```javascript
var x = 0
while true {
    x = x + 1
    if x % 2 == 0 { next }   // skip evens
    if x > 9      { break }
    print(x)
}
```

---

## do-while

Body executes at least once before the condition is checked.

```javascript
var x = 0
do {
    x = x + 1
} while x < 5
```

---

## for-in

Iterates arrays, maps, and trees. Yields index/key and value.

```javascript
for i, v in [10, 20, 30] {
    print(i, v)   // 0 10 / 1 20 / 2 30
}

for k, v in { a: 1, b: 2 } {
    print(k, v)
}
```

Trees iterate in sorted key order:

```javascript
var t = tree.new()
t["b"] = 2
t["a"] = 1

for k, v in t {
    print(k)   // "a", then "b"
}
```

`break` and `next` work in `for` loops too.

---

## goto

Forward and backward jumps. Labels use `:name:` syntax.

```javascript
goto skip
print("never runs")
:skip:
print("here")
```

Useful for breaking out of nested loops:

```javascript
while x < 10 {
    while y < 10 {
        if x + y == 7 { goto done }
        y = y + 1
    }
    x = x + 1
}
:done:
```
