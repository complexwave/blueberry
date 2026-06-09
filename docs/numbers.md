# Numbers

## Introduction

Blueberry has two number representations:
- **Tagged pointer integers** — small integers stored directly in the pointer, no heap allocation. Arithmetic on tagged ints is fast.
Default number type is integer. Fast tagged integers are 31 bit or 63 bit sized depending on system pointer width - 1 bit

- **Boxed numbers (`ci_number`)** — heap-allocated objects, supports i128 integers and 64-bit floats. 
Created when a value doesn't fit in a tagged int, or explicitly via `number()` / `float()`.

When integer doesnt fit into tagged representation, its is promoted to int128 type automatically. 
This includes overflow of math operations! When i128 overflows, it is automatically promoted to double, with loss of precision but not magnitude
This ensures, that code similar to this
Bitops promote doubles to ints
Bitops operate on 128bit integers, mask with & for 32/64 bit overflow behaviour.


```javascript
var cnt = number(webserver.req.param.count)
var price = db.get().price

var total_price = cnt * price;

if( total_price > 1000 ){
	// reject
}

// total price will never overflow, if cnt is huge number result of multiplication will be promoted to double floats
// double floats can store huge values, although with precision loss
// never allowing check to fail with malicious data
```

Math operations with floats produce floats. Most of math namespace functions that are mathematical functions return floats.

```javascript

var boxed_double = 10 * 1.2; // 12.0

```

`math.type(x)` tells you which kind you have. In normal use the distinction is invisible — operators and `math` functions handle both transparently.

Despite being objects, integers are transparently comparent by value with `==` and similar operators.
`==` comparison of floating numbers is not exact by default, but uses global precision. If you want different behaviour use `math.eq` with specified precision, 0.0 is valid precision for exact comparison


**Storing boxed numbers as hash keys doesnt currently work** - will be findable only by exact same object. This is planned to be fixed


## Global constructors

### `number(x)`

Tries to parse string as integer. 
- A string → parsed as integer or float (scientific notation supported); returns `null` on parse failure
If supplied with integer returns integer itself

```javascript
var n = number("1.5e10")
n = number(42) 
n = number("   1.5e10   ") // whitespaces fine
n = number("1000random") // null, no trailing garbage

```

### `float(x)`

Like `number(x)` but additionally accepts `"inf"`, `"+inf"`, `"-inf"`, `"nan"` (case-insensitive). Returns `null` on failure.

```javascript
var inf = float("inf")
var nan = float("NaN")
var neg = float("-inf")
```

---

## `math` namespace

All `math` functions accept both integers and floats. Most mathematical functions promote to float and return float. Functions that select among their inputs (`min`, `max`, `clamp`) return the winning value unchanged — type is preserved.

## Constants

| Name | Value |
|------|-------|
| `math.pi` | π ≈ 3.14159265358979 |
| `math.e` | e ≈ 2.71828182845904 |
| `math.inf` | Positive infinity |
| `math.nan` | Not-a-number |
| `math.huge` | Largest representable float (`HUGE_VAL`) |
| `math.u32` | `0xFFFFFFFF` (tagged int) |
| `math.u64` | `0xFFFFFFFFFFFFFFFF` (boxed i128) |

```javascript
var area = math.pi * r * r
```

## Functions

| Function | Description |
|----------|-------------|
| `math.sin(x)` | Sine (radians) |
| `math.cos(x)` | Cosine (radians) |
| `math.tan(x)` | Tangent (radians) |
| `math.asin(x)` | Arc sine → radians |
| `math.acos(x)` | Arc cosine → radians |
| `math.atan(y, x?)` | Arc tangent of `y/x` → radians; `x` defaults to `1.0` |
| `math.sqrt(x)` | Square root |
| `math.cbrt(x)` | Cube root |
| `math.pow(b, e)` | `b` raised to `e` |
| `math.hypot(a, b)` | Hypotenuse: `sqrt(a²+b²)` |
| `math.exp(x)` | `e^x` |
| `math.exp2(x)` | `2^x` |
| `math.log(x)` | Natural logarithm |
| `math.log2(x)` | Base-2 logarithm |
| `math.log10(x)` | Base-10 logarithm |
| `math.floor(x)` | Round down |
| `math.ceil(x)` | Round up |
| `math.round(x)` | Round to nearest |
| `math.trunc(x)` | Round toward zero |
| `math.abs(x)` | Absolute value |
| `math.sign(x)` | `-1`, `0`, or `1` (returns int) |
| `math.fmod(x, y)` | Floating-point remainder |
| `math.deg(x)` | Radians → degrees |
| `math.rad(x)` | Degrees → radians |
| `math.clamp(x, lo, hi)` | Clamp `x` to `[lo, hi]`; returns `lo`, `x`, or `hi` unchanged |
| `math.min(a, b, ...)` | Minimum of one or more values; returns the winning argument unchanged |
| `math.max(a, b, ...)` | Maximum of one or more values; returns the winning argument unchanged |
| `math.isnan(x)` | True if `x` is NaN |
| `math.isinf(x)` | True if `x` is ±infinity |
| `math.isfinite(x)` | True if `x` is finite |
| `math.eq(a, b, eps?)` | True if `\|a−b\| ≤ eps` |
| `math.cmp(a, b, eps?)` | `-1`/`0`/`1` with epsilon tolerance |
| `math.precision(eps?)` | Get or set default epsilon for `eq`/`cmp` |
| `math.type(x)` | `"int"` or `"float"`, `null` if not a number |
| `math.boxed(x)` | True if `x` is a boxed number, false if tagged int |
| `math.dividebyzero(val?)` | Get or set divide-by-zero behaviour (throw vs. inf/nan) |

---

### `math.atan(y, x?)`

One-argument form returns `atan(y)`. Two-argument form returns `atan2(y, x)` — the angle of the vector `(x, y)` in radians, handling all quadrants correctly.

```javascript
math.atan(1)           // π/4
math.atan(1, 0)        // π/2  (pointing straight up)
math.atan(-1, -1)      // -3π/4
```

### `math.clamp(x, lo, hi)`

Returns `x` clamped to `[lo, hi]`. Returns one of the three arguments as-is — no type conversion.

```javascript
math.clamp(1.5, 0, 1)    // 1.5 → 1   (returns hi, which is int 1)
math.clamp(-3, 0, 10)    // -3  → 0   (returns lo, which is int 0)
math.clamp(5, 0, 10)     // 5          (returns x unchanged)
```

### `math.min(a, b, ...)` / `math.max(a, b, ...)`

Accept any number of arguments. Return the winning argument unchanged — no type conversion.

```javascript
math.min(3, 1, 4, 1, 5)   // 1   (int)
math.max(3, 1, 4, 1, 5)   // 5   (int)
math.max(1, 2.5)           // 2.5 (float)
```

### `math.sign(x)`

Returns `-1`, `0`, or `1` as an integer.

```javascript
math.sign(-5.0)   // -1
math.sign(0.0)    // 0
math.sign(3.0)    // 1
```

### `math.deg(x)` / `math.rad(x)`

Convert between degrees and radians.

```javascript
math.deg(math.pi)    // 180.0
math.rad(180)        // π
```

---

## Floating-point comparison

Integer `==` is exact. Float comparison is tricky — two floats that should be equal may differ by a small rounding error. `math.eq` and `math.cmp` use an epsilon tolerance for this.

### `math.eq(a, b, eps?)`

Returns `true` if `|a − b| ≤ eps`. Default `eps` is `1e-9` (configurable via `math.precision`).

```javascript
math.eq(0.1 + 0.2, 0.3)         // true
math.eq(0.1 + 0.2, 0.3, 1e-15)  // may be false — tight tolerance
```

### `math.cmp(a, b, eps?)`

Returns `-1`, `0`, or `1`. Values within `eps` of each other compare as equal (`0`).

```javascript
math.cmp(1.0, 2.0)   // -1
math.cmp(1.0, 1.0)   // 0
math.cmp(2.0, 1.0)   // 1
```

### `math.precision(eps?)`

With no argument, returns the current default epsilon. With an argument, sets it and returns the previous value.

```javascript
var old = math.precision(1e-6)   // set new, get old
math.precision()                  // 1e-6
```

### `math.dividebyzero(val?)`

Controls what happens when an integer is divided by zero.

- No argument / `null` — returns current setting: `true` = throws, `false` = produces `inf`/`nan`.
- Any value — sets the behaviour (truthy = throw, falsy = inf/nan) and returns the previous setting.

```javascript
math.dividebyzero()        // false by default — produces inf/nan
math.dividebyzero(true)    // now throws on integer ÷ 0
math.dividebyzero(false)   // back to inf/nan
```

---

## Type inspection

### `math.type(x)`

Returns `"int"` or `"float"` for numbers, `null` for anything else.

```javascript
math.type(42)     // "int"
math.type(3.14)   // "float"
math.type("hi")   // null
```

### `math.boxed(x)`

Returns `true` if `x` is a boxed `ci_number` (heap-allocated float or large int), `false` if it is a tagged integer, `null` if not a number at all. Mostly useful for debugging and low-level optimisation.

---

## Operators

Numbers support all standard arithmetic and bitwise operators. Both tagged ints and boxed numbers work with all of these.

### Arithmetic

| Operator | Description |
|----------|-------------|
| `a + b` | Addition |
| `a - b` | Subtraction |
| `a * b` | Multiplication |
| `a / b` | Division |
| `a % b` | Modulo / remainder |
| `-a` | Negation |

### Bitwise

| Operator | Description |
|----------|-------------|
| `a & b` | Bitwise AND |
| `a \| b` | Bitwise OR |
| `a ^ b` | Bitwise XOR |
| `a << n` | Left shift by `n` |
| `a >> n` | Right shift by `n` |
| `~a` | Bitwise NOT (complement) |

### Comparison

Numbers compare with `<`, `<=`, `>`, `>=`, `==`, `!=`. Integer `==` is exact. For float equality with tolerance use `math.eq`.
