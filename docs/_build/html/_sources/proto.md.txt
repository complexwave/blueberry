# Prototypes

## Introduction

Blueberry's prototype system lets maps inherit methods from a shared prototype — a lightweight way to define types without a full class system.

Any map can have a prototype. When a key is not found on the map itself, lookup continues up the prototype chain. Methods defined on the prototype are shared across all instances.

Built-in types (string, array, number, tree) have C-level prototypes registered at VM startup. User-defined prototypes are plain maps registered by name.

---

## `proto` namespace

| Function | Description |
|----------|-------------|
| `proto.set(obj, proto)` | Set the prototype of a map |
| `proto.get(obj)` | Get the user-set prototype of a map |
| `proto.of(obj)` | Get the prototype of any value (maps + builtins) |
| `proto.register(name, map)` | Register a named prototype globally |
| `proto.typeof(obj)` | Typename string of any value |
| `proto.all()` | The VM prototype registry (map of name → proto) |

The global `type(obj)` function is an alias for `proto.typeof(obj)`.

---

### `proto.set(obj, proto)`

Sets the prototype on a map. `proto` can be:
- A map — used directly
- A string — looked up by name in the prototype registry
- `null` — clears the prototype

Returns `obj`.

```javascript
var MyProto = { greet: fn(self) { print("hello from " + self.name) } }
proto.register("MyType", MyProto)

var obj = { name: "world" }
proto.set(obj, "MyType")
obj.greet()   // hello from world
```

### `proto.register(name, map)`

Registers a named prototype in the VM-wide registry. Sets `map.typename = name` automatically. Errors if the name is already taken.

Once registered, the name can be passed to `proto.set` as a string.

```javascript
var Animal = {}
Animal.speak = fn(self) { print(self.sound) }
proto.register("Animal", Animal)
```

### `proto.get(obj)`

Returns the user-set prototype of a map, or `null` if none is set or `obj` is not a map. Does not return built-in C-level prototypes.

### `proto.of(obj)`

Returns the prototype of any value — including built-in types. Useful for inspecting or extending the prototype chain.

```javascript
proto.of([])         // the array prototype map
proto.of("hi")       // the string prototype map
proto.of(42)         // null (tagged ints have no prototype)
```

### `proto.typeof(obj)` / `type(obj)`

Returns the typename as a string. For objects with a registered prototype, returns the registered name. For primitives and untyped maps, returns a built-in type name.

| Value | Result |
|-------|--------|
| `null` | `"null"` |
| `true` / `false` | `"bool"` |
| tagged int | `"int"` |
| untyped map | `"map"` |
| string | `"string"` |
| array | `"array"` |
| function | `"function"` |
| registered prototype instance | the registered name |

```javascript
type(42)          // "int"
type("hi")        // "string"
type({})          // "map"

var obj = {}
proto.set(obj, "Animal")
type(obj)         // "Animal"
```

### `proto.all()`

Returns the VM prototype registry — a map from name to prototype map. Includes all built-in and user-registered prototypes.

---

## Patterns

### Defining a type

```javascript
var Vec2 = {}

Vec2.new = fn(x, y) {
    var v = { x: x, y: y }
    proto.set(v, "Vec2")
    return v
}

Vec2.len = fn(self) {
    return math.sqrt(self.x * self.x + self.y * self.y)
}

Vec2.add = fn(self, other) {
    return Vec2.new(self.x + other.x, self.y + other.y)
}

proto.register("Vec2", Vec2)

var a = Vec2.new(3, 4)
a.len()              // 5
type(a)              // "Vec2"
```

### Prototype inheritance

Set a prototype's prototype to build a chain. Lookup walks up until the key is found or the chain ends.

```javascript
var Base = { hello: fn(self) { print("hi") } }
proto.register("Base", Base)

var Child = {}
proto.set(Child, "Base")
proto.register("Child", Child)

var obj = {}
proto.set(obj, "Child")
obj.hello()   // found via Base
```

---

## Limitations

- `proto.set` only works on maps. Built-in types (arrays, strings, etc.) have fixed C-level prototypes that cannot be replaced from script.
- Prototype names are global and permanent — `proto.register` errors if the name is already taken.
- No `super` keyword. Calling a parent method requires an explicit reference: `Base.method(self, ...)`.
