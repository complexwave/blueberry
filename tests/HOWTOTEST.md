# How to write tests for Blueberry

lua test suite /home/user/cobj/lua-5.5.0-tests/
when starting conversation list lua tests you consider basic and offer to choose which to implement
dont read all lua test files choose based on names

read /home/user/cobj/scripts/prototype_test.ci to understand harness, follow test format with
test name
pass fail correct output type

there is specific skip function.
dont delete failing tests. comment them out but leave failing code if it crashes vm for me to read

DSO NOT DO THIS
test.fail("anon_function_recursion_SKIP_BUG")

DO
test.skip("anon_function_recursion")

do not rename tests when skipping name must be same.

failing tests when you create tmp scripts and run them to figure syntax:
use tests/drafts/ and save all weird shit that fails there to inspect later


var point = {}
    point.distance = fn() {                                                                                                                                           
          return point.x * point.x + point.y * point.y                                                                                                                  
        }                                                                                                                                                               
no closures. wont work. function wont see point from outside.

workarounds: point as global or self. and function as method

ALWAYS add before runnign test so if it crashes its easy to see where
print("running tesat_name")
THIS IS NECESSARY WHEN PORTING TEST TO ADD THIS PRINT
it saves you tonn of debug time on what crashes

als, do not write like this

// Can't test with floats - no float support
test.skip("float_arithmetic")

when porting lua tests. write code and comment it out with //
then add skip. so when i decide to implement it I can just uncomment

Thanks <3


## Project context

Blueberry is a scripting language. Source files use `.ci` extension. The VM binary is `./blueberry` in the project root (`/home/user/cobj/`). Language syntax reference is in `/home/user/cobj/syntax.md`. Known bugs list is `/home/user/cobj/known_bugs.txt`.

## Structure

- VM binary: `./blueberry <script.ci>` — runs a single script
- Test helpers: `tests/test_helpers.ci`
- Test files: `tests/` and subdirs (e.g. `tests/syntax/`)
- Bash harness: `./tests.sh` (runs all `tests/**/*.ci` except `test_helpers.ci`)
- Known bugs: `known_bugs.txt` — add new bugs here when found during testing
- Rebuild VM: `make blueberry`

## Running

```bash
make blueberry                        # rebuild after C changes
./blueberry tests/syntax/assign.ci    # single test
./tests.sh                            # all tests
```

### Test harness output

The bash harness (`./tests.sh`) runs all test files and reports:
- **Per-file summary**: filename, pass count, fail count, skip count, exit status
- **Skipped section**: lists all skipped tests by filename and test name
- **Failures section**: lists all failed tests by filename and test name
- **Final summary**: total files, OK count, FAIL count, and skipped count

Example output:
```
OK    literals (5 passed)
OK    assign (8 passed, 1 skipped)
FAIL  constructs (3 passed, 1 failed, exit=1)

===============================
Files: 3  OK: 2  FAIL: 1
Skipped tests: 1
===============================

Skipped:
--- assign ---
<SKIP for_loop_edge_case>

Failures:
--- constructs ---
<FAIL if_nested_bug>
```

## Writing a test file

```ci
var h = require("tests/test_helpers.ci")
h()

print("running my_test_name")
test.equal(1 + 1, 2, "my_test_name")

print("running another_test")
test.assert(true, "another_test")

test.done()
```

### Rules

1. **Always `print("running testname")` before each test** — when VM crashes, last printed line tells you which test caused it
2. **require returns a function** — you must call it: `var h = require(...); h()`
3. **Globals from require**: `test_helpers.ci` uses bare assignment (no `var`) so `test` is a global visible after `h()`
4. **No closures**: `fn` cannot access outer `var`s. Only `self`, function args, and globals are available. This is by design, not a bug.
5. **Skipping vs failing**: 
   - **Skip**: `test.skip("name")` — for known bugs, unimplemented features, or tests waiting on other work. Shows in `Skipped:` section with filename.
   - **Fail**: `test.fail("name_BUG_description")` — only for tests you want to track as known failures (rare; prefer skip)
   - **Never** append `_SKIP_BUG` to the test name — that's a naming error. Use `test.skip()` instead.
6. **Don't debug crashes** — just comment out the crashing line, mark it as a known bug, and move on

### Test helpers API

- `test.equal(got, expected, name)` — equality check, prints expected vs got on failure
- `test.assert(cond, name)` — truthy check
- `test.pass(name)` — manual pass
- `test.fail(name)` — manual fail (use for known bugs)
- `test.skip(name)` — skip a test (counts toward skipped total, shows in harness output)
- `test.done()` — prints summary, calls `exit(1)` if any failures

## Language quick reference (for writing tests)

### Variables & assignment
```ci
var x = 1           // declare + assign
x = x + 1          // reassign
x += 1             // compound assign (works on locals only!)
var a, b = multi()  // multi-assign from multi-return
```

### Types & literals
```ci
42  0xFF  0b1010    // int (no floats yet!)
"hello"  'world'   // strings (both quote styles)
true  false  null   // constants
[1, 2, 3]          // array
{ "k" => v }       // map (=> separates key/value pairs)
```

### Falsy values
`0`, `false`, `null` — everything else is truthy (including `""`, `[]`, `{}`)

### Functions
```ci
fn name(a, b) { return a + b }     // named
var f = fn(x) { return x * 2 }    // anonymous
fn obj.method() { ... }           // dotted — self = receiver
return x, y                       // multi-return
```

### Control flow
```ci
if cond { }                        // parens optional
if cond { } else if cond2 { } else { }
while cond { }
do { } while cond
for i, v in arr { }               // array: i=index, v=value
for k, v in map { }               // map: k=key, v=value
break
next                               // = continue
:label:
goto label
```

### Maps (objects)
```ci
var m = {}
m.key = val                        // dot access
m["key"] = val                     // subscript access
m.a.b.c                           // null-propagates (returns null, no crash)
```

### Arrays
```ci
var a = [1, 2, 3]
a[0]               // index
a.len()            // length
a.push(x)          // append
a.pop()            // remove last
a.shift()          // remove first
a.unshift(x)       // prepend
a.resize(n)        // grow/shrink
```

### Prototypes
```ci
var proto = {}
fn proto.new() {
    var obj = {}
    setprototype(obj, self)
    return obj
}
fn proto.greet() { print(self) }
var inst = proto.new()
inst.greet()                       // self = inst
```

### Built-in functions
```ci
print(a, b, ...)       // space-separated + newline
type(x)                // "int", "string", "bool", "null", "array", "map"
setprototype(obj, p)   // returns obj
stacktrace()           // call stack as string
exit(code)             // terminate process
file_read(path)        // string or null
file_write(path, data) // true or null
file_exists(path)      // true/false
dir_list(path)         // array of strings
dir_exists(path)       // true/false
require("file.ci")     // returns root fn — must call it
```

### Operators (all work on locals)
```
+  -  *  /  %  **            // arithmetic
==  !=  <  >  <=  >=         // comparison
&&  ||  !                     // logical
&  |  ^  ~  <<  >>           // bitwise
??                            // null-coalesce: a ?? b
+=  -=  *=  /=  %=  **=     // compound assign
<<=  >>=  |=  &=  ^=        // compound bitwise
||=  &&=                      // compound logical
x++  x--                      // post-increment/decrement (statement only)
```

### Strings
```ci
var s = "hello"
s.len()                // length
s.copy()               // clone (needed before .append since literals share memory)
s.copy().append(b)     // this is how you concatenate strings
```

## Workarounds & known bugs

These are confirmed broken — don't waste time debugging, just work around them:

| Bug | Workaround |
|-----|-----------|
| `self.x += 1` no-op | `self.x = self.x + 1` |
| `m.field += 1` no-op | `m.field = m.field + 1` |
| `m["k"] += 1` no-op | `m["k"] = m["k"] + 1` |
| `??=` crashes VM | skip entirely |
| `typeof x` keyword broken | use `type(x)` function |
| Float literals | not implemented, integers only |
| `+` on strings | use `.copy().append()` |
| `setprototype` in tests | crashes — may need investigation |

**Root cause pattern**: compound assign (`+=` etc) works on local variables but NOT on map field access (dot or subscript). The codegen treats the LHS as an expression, computes the result, but doesn't emit the store-back to the map.

## Naming conventions

- Test names: `snake_case`, descriptive (e.g. `local_add_assign`, `map_field_manual_add`)
- Bug markers: append `_BUG_description` or `_SKIP_BUG` to test name
- Files: topic-based (e.g. `assign.ci`, `literals.ci`, `constructs.ci`)
- Subdirs: group by category (`tests/syntax/`, `tests/literals.ci`)

## Test plan (adapted from Lua 5.5 test suite)

Priority order:
1. `literals` — types, constants, equality ✓ done
2. `syntax/assign` — assignment variants ✓ done
3. `math` — arithmetic, precedence, edge cases
4. `bitwise` — bitwise operators
5. `constructs` — if/while/for/break/next/goto
6. `calls` — functions, recursion, multi-return
7. `strings` — string methods
8. `arrays` — array methods, iteration
9. `maps` — map operations, prototypes
10. `sort` — comparison callbacks (needs array.sort or manual)
