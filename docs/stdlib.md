# Standard Library

```{note}
The stdlib is under active development. This page will be expanded as APIs stabilize.
```

## Builtins

### print(...)

Print values to stdout.

```javascript
print("hello", 42, true)
```

### str(x) / string.from(x)

Convert any value to a human-readable string. Same function, two names.

| Input | Output |
|-------|--------|
| `null` | `"null"` |
| `true` / `false` | `"true"` / `"false"` |
| string | mutable copy |
| number | `%g` formatting — `42.0` → `"42"`, `3.14` → `"3.14"` |
| anything else | `"[typename 0xADDR]"` |

```javascript
str(42)        // "42"
str(3.14)      // "3.14"
str(true)      // "true"
str(null)      // "null"
str("hi")      // mutable copy of "hi"
str([1,2,3])   // "[array 0x...]"
```

## String Methods

- `.copy()` — explicit copy
- `.append(str)` — append in place
- `.len()` — string length

## Array Methods

- `.push(val)` — append element
- `.len()` — array length

## Map Operations

- `map.key = val` — set field
- `map.key` — get field

## Proto

- `proto.set(obj, prototype)` — set prototype for object
