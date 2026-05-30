# Learn Blueberry in 15 Minutes

Blueberry is a dynamic scripting language written in C, inspired by Lua and JavaScript.
It aims to be fast, predictable, and not annoying. If you know JS and Lua, you're 90% there.

```ci
// This is a comment. Only single-line // comments exist.
// File extension is .ci
// Run with: ./blueberry myscript.ci
```

## Variables

```ci
var x = 1               // declare with var
var name = "blueberry"
var a, b, c             // declare multiple (initialized to null)
var p, q = multi()      // multi-assign from multi-return function

x = 2                   // reassign (no var keyword)

// Declaring without value gives null
var y;                  // y is null
```

## Types and Literals

```ci
// Integers (no floats yet, but doubles are boxed transparently)
42          // decimal
0xFF        // hex -> 255
0b1010      // binary -> 10
-1          // negative

// Strings (both quote styles, mutable)
"hello"
'world'
""          // empty string

// Escape sequences
"\n"        // newline
"\t"        // tab
"\\"        // backslash
"\""        // double quote

// Booleans
true
false

// Null
null

// Arrays (0-indexed, unlike Lua!)
[1, 2, 3]
[]          // empty array

// Maps
{}                          // empty map
{ key: "value" }            // shorthand keys
{ "key" => "value" }        // string keys with arrow syntax

// Check types with type()
type(42)        // "int"
type("hi")      // "string"
type(true)      // "bool"
type(null)      // "null"
type([])        // "array"
type({})        // "map"
```

## Falsy and Truthy

```ci
// Falsy: 0, false, null
// Truthy: EVERYTHING else, including "", [], {}
// (empty string is truthy — unlike JS!)

if 0     { }  // skipped
if false { }  // skipped
if null  { }  // skipped
if ""    { }  // runs!
if []    { }  // runs!
if {}    { }  // runs!
```

## Operators

```ci
// Arithmetic
1 + 2       // 3
10 - 3      // 7
6 * 7       // 42
10 / 2      // 5
10 % 3      // 1
2 ** 3      // 8  (power)

// Comparison
a == b      a != b
a < b       a > b
a <= b      a >= b

// Logical
a && b      // and
a || b      // or
!a          // not

// Bitwise
a & b       // AND
a | b       // OR
a ^ b       // XOR
a << 2      // left shift
a >> 2      // right shift

// Increment / decrement
x++         // post-increment
x--         // post-decrement (works in while conditions too)

// Null coalescing
a ?? b      // b if a is null, else a
```

## Compound Assignment

```ci
x += 1      x -= 1      x *= 2      x /= 2      x %= 3
x **= 2    x <<= 1     x >>= 1
x |= mask  x &= mask   x ^= mask

// Logical compound assign
x ||= 42       // assign 42 if x is falsy
x &&= 42       // assign 42 if x is truthy
x ??= 42       // assign 42 if x is null (keeps 0, false, "")

// All compound assigns work on map fields too
m.value += 10
m["key"] += 1
obj.inner.count *= 2
```

## Control Flow

```ci
// If / else if / else (no parens needed around condition)
if x > 0 {
    print("positive")
} else if x == 0 {
    print("zero")
} else {
    print("negative")
}

// While loop
while x < 10 {
    x += 1
}

// Do-while (runs body at least once)
do {
    x -= 1
} while x > 0

// For-in loop (arrays and maps)
var arr = [10, 20, 30]
for idx, val in arr {
    print(idx, val)     // 0 10, 1 20, 2 30
}

var map = { a: 1, b: 2 }
for key, val in map {
    print(key, val)
}

// Break and next (next = continue)
while true {
    if done { break }
    if skip { next }    // skip to next iteration
}

// Goto and labels
goto skip
print("this is skipped")
:skip:
print("landed here")

// Labels for loops
:retry:
if failed {
    goto retry
}
```

## Functions

```ci
// Named function
fn add(a, b) {
    return a + b
}

// Anonymous function (assigned to variable)
var double = fn(x) {
    return x * 2
}

// Functions are first-class values
var apply = fn(f, x) { return f(x) }
apply(double, 5)    // 10

// Multi-return
fn swap(a, b) {
    return b, a
}
var x, y = swap(1, 2)

// Self-recursion in anonymous functions uses `self`
var fib = fn(n) {
    if n <= 1 { return 1 }
    return self(n - 1) + self(n - 2)
}
```

**Important: No closures.** Functions cannot capture outer `var`s.
Only `self`, function arguments, and globals are available inside `fn`.

```ci
var x = 10
var broken = fn() { return x }  // WON'T WORK -- x not visible

// Workarounds: pass as argument, use globals, or use self with methods
```

## Strings

```ci
var s = "hello"

// String methods
s.len()                     // 5
"".len()                    // 0

// Strings are mutable. Concatenation uses copy + append:
var greeting = s.copy().append(" world")    // "hello world"
// Original is unchanged:
print(s)                    // "hello"

// Chained append
var full = "a".copy().append("b").append("c")   // "abc"

// String comparison (lexicographic)
"abc" < "abd"       // true
"a" < "b"           // true
"abc" == "abc"      // true

// String + number concatenation
"result: " + 42     // works via + operator
```

## Arrays

```ci
var a = [1, 2, 3]

// Access (0-indexed!)
a[0]                // 1
a[2]                // 3

// Methods
a.len()             // 3         -- length
a.push(4)           // append to end
a.pop()             // remove and return last element
a.unshift(0)        // prepend to beginning
a.shift()           // remove and return first element
a.resize(10)        // grow (fills with null) or shrink
a.size()            // allocated capacity (>= len)

// Out-of-bounds read returns null (no crash)
[][0]               // null
[][99]              // null

// Out-of-bounds write auto-extends the array
var b = []
b[3] = 99           // b is now [null, null, null, 99]
```

## Maps (Objects)

```ci
var m = {}

// Dot access
m.name = "test"
print(m.name)

// Subscript access (dynamic keys)
m["port"] = 8080
var key = "port"
m[key]              // 8080

// Literal with values
var person = { name: "Alice", age: 30 }
var dict = { "key1" => "value1", "key2" => "value2" }

// Nested maps
var obj = { inner: { count: 100 } }
obj.inner.count     // 100

// Null propagation on missing keys (no crash!)
var empty = {}
empty.x             // null
empty.x.y.z         // null (propagates safely)

// Store functions in maps
m.greet = fn() { return "hi" }
m.greet()           // "hi"
```

## Methods and Self

```ci
// When a function is called via dot syntax, `self` is the receiver
var rect = {}
rect.w = 10
rect.h = 5
rect.area = fn() {
    return self.w * self.h
}
rect.area()         // 50

// Named method syntax
fn rect.scale(factor) {
    self.w = self.w * factor
    self.h = self.h * factor
    return self     // enables chaining
}
rect.scale(2)       // rect.w=20, rect.h=10

// Method chaining
var r = rect.scale(3)
print(r.w)          // 60
```

## Prototypes (OOP)

```ci
// Prototype-based inheritance (like JS prototypes)
var animal = {}
animal.name = "creature"
fn animal.speak() {
    return self.name
}

// Create child, set prototype
var dog = {}
dog.name = "Rex"
proto.set(dog, animal)      // dog inherits from animal
// also: setprototype(dog, animal)

dog.speak()                 // "Rex" (self = dog, not animal)

// Prototype chain
var puppy = {}
proto.set(puppy, dog)
puppy.speak()               // finds speak() via dog -> animal
puppy.name                  // "Rex" (inherited from dog)

// Constructor pattern
var Point = {}
fn Point.new(x, y) {
    var obj = {}
    setprototype(obj, self)
    obj.x = x
    obj.y = y
    return obj
}
fn Point.magnitude() {
    return self.x * self.x + self.y * self.y
}
var p = Point.new(3, 4)
p.magnitude()               // 25

// Child overrides parent
var parent = {}
fn parent.compute() { return 1 }
var child = {}
fn child.compute() { return 2 }
proto.set(child, parent)
child.compute()             // 2 (child's version wins)
```

## Modules

```ci
// require() loads and executes a file, returns its last expression
var h = require("tests/test_helpers.ci")
h()     // require returns a function here — must call it

// Globals: assigning without `var` creates a global
greeting = "hello"      // global, visible in required files
```

## Built-in Functions

```ci
print(a, b, ...)        // print space-separated values + newline
type(x)                 // returns type as string
setprototype(obj, p)    // set prototype, returns obj
proto.set(obj, p)       // same as above
require("path.ci")      // load and execute a module
exit(code)              // terminate with exit code
stacktrace()            // returns call stack as string
file_read(path)         // read file contents, returns string or null
```

## Putting It Together

```ci
// A tiny linked list
var Node = {}
fn Node.new(val) {
    var n = {}
    setprototype(n, self)
    n.val = val
    n.next = null
    return n
}

var head = Node.new(1)
head.next = Node.new(2)
head.next.next = Node.new(3)

// Walk it
var curr = head
while curr {
    print(curr.val)
    curr = curr.next
}
// prints: 1  2  3
```
