# Syntax

Blueberry syntax is familiar if you know JS or Lua. No inventions — just the existing scripting paradigm done better.

## Variables

Variables are declared with `var`. Uninitialized variables default to `nil`.

```javascript
var name = "Blueberry"
var x = 42
var pi = 3.14
var unset  // nil
```

Globals are assigned without `var`:

```javascript
MyGlobal = 100
```

## Types

Blueberry is dynamically typed with no implicit type coercion (except integer widening in arithmetic, like C).

| Type | Example |
|------|---------|
| nil | `nil` |
| bool | `true`, `false` |
| int | `42`, `0xFF` |
| double | `3.14` |
| string | `"hello"` |
| array | `[1, 2, 3]` |
| map | `{ key: "value" }` |
| function | `fn(x) { return x }` |

Integers are native-width (up to `size_t - 1`), with transparent boxing to double or int128 when needed.

## Strings

Strings are **mutable**. Concatenation must be explicit — there is no `+` overload for strings.

```javascript
var s = "hello"
var greeting = s.copy().append(" world")
```

Use `.copy()` when you need a separate copy. Without it, you're mutating in place.

## Arrays

Arrays are **0-indexed**. No Lua 1-based nonsense.

```javascript
var nums = [10, 20, 30]
nums.push(40)

print(nums[0])    // 10
print(nums.len()) // 4
```

## Maps

Maps use `{}` with JS-like shorthand keys.

```javascript
var config = { host: "localhost", port: 8080 }
config.debug = true

print(config.host)
print(config["port"])  // bracket access also works
```

## Operators

### Arithmetic

`+`, `-`, `*`, `/`, `%` — standard arithmetic. Integer arithmetic stays integer; mixing with doubles promotes to double.

### Comparison

`==`, `!=`, `<`, `>`, `<=`, `>=`

### Logical

`and`, `or`, `not` — short-circuit evaluation.

```javascript
if x > 0 and x < 100 {
    print("in range")
}
```

## Control Flow

### if / else

```javascript
if x > 10 {
    print("big")
} else if x > 5 {
    print("medium")
} else {
    print("small")
}
```

### for-in

Iterate over arrays with index and value:

```javascript
for i, v in nums {
    print(i, v)
}
```

### while

```javascript
var i = 0
while i < 10 {
    print(i)
    i = i + 1
}
```

### break

```javascript
while true {
    if done {
        break
    }
}
```

## Functions

Declared with `fn`. First-class values.

```javascript
fn add(a, b) {
    return a + b
}

print(add(2, 3))  // 5
```

Methods on objects receive `self` implicitly:

```javascript
fn Vec.len2() {
    return self.x * self.x + self.y * self.y
}
```

## Prototype OOP

Objects use prototype-based inheritance. Set a prototype with `proto.set()`, method calls resolve up the chain via `self`.

```javascript
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
