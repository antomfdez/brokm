# brokm syntax (v0.1)

A HolyC-flavored, C-like language. This documents what v0.1 actually implements.

## Comments

```c
// line comment
/* block comment */
```

## Types

`U0 U8 U16 U32 U64 I8 I16 I32 I64 F64 Bool`

In v0.1 the runtime is dynamically represented (values are tagged `Int`, `Float`, `Bool`,
`Nil`, or an object). Type annotations are accepted and drive declarations and parameter lists;
**full static type checking arrives in v0.4**. The default integer is `I64`; `F64` is the
floating type.

## Literals

```c
42            // integer
0xFF          // hex integer
3.14   2.0e3  // float
'A'  '\n'     // character (an integer code)
"text\n"      // string  (escapes: \n \t \r \0 \\ \" \')
TRUE  FALSE   // booleans
NULL          // nil
```

## Declarations

Type-first. Top-level declarations create globals; declarations inside a block are locals.

```c
I64 count = 10;
F64 ratio = 0.5;
Bool ready = TRUE;
U8   name  = "brokm";   // a string value
I64  later;             // defaults to nil until assigned
```

## Functions

```c
I64 Add(I64 a, I64 b)
{
  return a + b;
}

U0 Greet()        // U0 = returns nothing
{
  "hi\n";
}
```

Functions are callable with parentheses: `Add(1, 2)`. Recursion is supported. Top-level
code may appear before or after function definitions; calls execute at runtime in program order.

## Printing (HolyC style)

A statement that begins with a string literal is printed. Trailing comma-separated arguments
fill printf-style specifiers:

```c
"Hello World\n";
"x = %d\n", x;
"%s is %d years old\n", name, age;
"%g\n", 3.0 / 2.0;     // 1.5
"100%%\n";             // literal percent
```

Supported specifiers: `%d %i` (integer), `%u %o %x %X` (unsigned), `%e %E %f %F %g %G`
(float), `%c` (char), `%s` (string), `%%`. Width/precision/flags pass through, e.g. `%5.2f`.
`Print(...)` is the same formatter as a named native.

## Arrays

Dynamic, heap-allocated, holding any mix of values (including other arrays):

```c
I64[] a = [10, 20, 30];   // literal; the [] in the type is optional decoration
a[1] = 25;                // index assignment
"%d\n", a[0];             // index read
Append(a, 40);            // grow by one element
"%d\n", Len(a);           // 4
U0[] grid = [[1, 0], [0, 1]];
"%d\n", grid[1][1];       // nesting
```

Indices are integers; out-of-bounds access and indexing a non-array are runtime
errors.

## Classes / structs

`class` (or `struct`) declares a reference-semantics object type with typed fields:

```c
class Point { I64 x; I64 y; }

Point p = Point(3, 4);   // construct by calling the class (positional fields)
Point q;                 // default-construct; fields start nil
"%d\n", p.x;             // field read
p.x = 10;                // field assignment
Point r = p;             // r and p reference the SAME instance
r.y = 99;                // ...so this also changes p.y
```

- Instances live on the heap and are passed by reference (assignment/argument
  passing shares the instance, it does not copy).
- Construction sets fields positionally; omitted trailing fields are nil.
- Only declared fields may be read or written; touching an unknown field is a
  runtime error. Fields may hold any value, including arrays and other instances.
- `class` and `struct` are synonyms. There are no pointers or methods yet (see
  the roadmap); behavior is written as free functions taking instances.

## Built-in functions

- `Print(fmt, ...)` — same printf-style formatter as a bare string statement.
- `Len(x)` — element count of an array, or character count of a string.
- `Append(array, value)` — grow an array by one element; returns the array.
- `GcCollect()` — force a full (major) garbage collection (returns nil).
- `GcMinor()` — force a minor (young-generation) collection (returns nil).

## Control flow

```c
if (cond) { ... } else { ... }

while (cond) { ... }

for (I64 i = 0; i < n; ++i) { ... }

do { ... } while (cond);

switch (x)
{
  case 1: "one\n"; break;
  case 2: "two\n"; break;
  default: "other\n"; break;
}

break;      // exit nearest loop or switch
continue;   // next iteration of nearest loop
return expr;
```

## Operators

| Category    | Operators |
|-------------|-----------|
| Arithmetic  | `+ - * / %` (integer `/` truncates; `%` is integer-only) |
| Strings     | `+` concatenates two strings (`"a" + "b"` → `"ab"`) |
| Comparison  | `== != < <= > >=` |
| Logical     | `&& || !` (short-circuit) |
| Bitwise     | `& | ^ ~ << >>` (integer-only) |
| Assignment  | `=` and compound `+= -= *= /= %= &= |= ^= <<= >>=` |
| Inc/Dec     | `++ --` (prefix and postfix) |

Truthiness: `nil` and `FALSE` are false; `0` / `0.0` are false; everything else is true.

## Known v0.1 simplifications

- Postfix `x++` evaluates to the **new** value (like prefix). Fine in loop increments; rarely
  matters elsewhere. Will be made fully correct alongside typed bytecode.
- `continue` inside `do/while` jumps to the top of the body (re-runs it) rather than to the
  condition test.
- Avoid declaring local variables directly inside `switch` case bodies (wrap logic in a function
  if needed) — case-body scoping is not modeled yet.
- One variable per declaration (no `I64 a, b;` yet).
- No pointers, structs/classes, `MAlloc/Free`, or modules yet — see ROADMAP.
