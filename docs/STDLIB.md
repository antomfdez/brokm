# brokm library reference

Every callable function available to a brokm program, grouped by library.

Two layers:

1. **Built-in natives** — C primitives registered in `src/natives.c`. Always
   available, no `#include` needed.
2. **Standard library** — brokm written in brokm under [`lib/std/`](../lib/std/).
   Pull it in with `#include "std/std.bk"` (everything) or a single module like
   `#include "std/str.bk"`. The include path resolves relative to the file, then
   `$BROKM_HOME/lib`, then `~/.brokm/lib` (see [SYNTAX](SYNTAX.md)).

Type notation follows the source: `U8` = string, `I64` = int, `F64` = float,
`Bool` = boolean, `U0` = any value, `U0[]` = array, `Ptr` = raw pointer (from
`MAlloc`). brokm has **one flat namespace**, so stdlib names are module-prefixed
(`Str*`, `Arr*`, `Path*`, …).

Conventions across the whole library:
- Strings are **interned and immutable**; `==` compares contents.
- Array helpers that reorder elements return a **new** array; the argument is
  untouched.
- Out-of-range / failure generally yields `-1`, `NULL`, `FALSE`, or `""` rather
  than an error — these are scripting primitives.

---

# Built-in natives (`src/natives.c`)

## Output & formatting

| Function | Returns | Description |
|---|---|---|
| `Print(fmt, ...)` | `nil` | printf-style formatting to **stdout**. If `fmt` is not a string, prints each argument space-separated. Integer conversions (`%d %i %u %o %x %X`) are 64-bit; `%c` takes a char code; `%s` takes a string. `%%` is a literal percent. |
| `PrintErr(fmt, ...)` | `nil` | Same formatter, targeting **stderr**. |

`Print` is the function behind the `"...", args;` print-statement sugar. A
missing argument for a conversion formats as `nil`/empty.

## Garbage collection control

| Function | Returns | Description |
|---|---|---|
| `GcCollect()` | `nil` | Force a full (major) collection now. |
| `GcMinor()` | `nil` | Force a minor (young-generation) collection now. |
| `GcDisable()` | `nil` | Suspend the collector — no pauses inside the bracketed window. Allocations made while disabled are reclaimable once re-enabled. |
| `GcEnable()` | `nil` | Resume the collector. |

## Arrays & maps (core)

| Function | Returns | Description |
|---|---|---|
| `Len(x)` | `I64` | Element count of an array, or character count of a string. `0` for anything else. |
| `Append(array, value)` | the array | Grow the array by one element (in place) and return it. |
| `MapNew()` | map | A new empty string-keyed map. |
| `MapSet(map, key, value)` | the map | Set `key` (a string) to `value`. |
| `MapGet(map, key)` | `U0` | Value for `key`, or `NULL` if absent. |
| `MapHas(map, key)` | `Bool` | Whether `key` is present. |
| `MapDelete(map, key)` | `Bool` | Remove `key`; `TRUE` if it existed. |
| `MapLen(map)` | `I64` | Number of live entries. |
| `MapKeys(map)` | `U0[]` | A new array of the keys (order unspecified). |

Map keys must be strings; values may be anything.

## Manual memory

Raw, GC-invisible memory for low-level work. `MAlloc` returns a `Ptr` the
collector never tracks or follows; it lives until `Free`. Access is by **typed
element index**: `PeekI64(p, 2)` reads the 3rd 8-byte slot. Bounds and lifetime
are the caller's responsibility — these are unchecked beyond `NULL`.

| Function | Returns | Description |
|---|---|---|
| `MAlloc(nbytes)` | `Ptr` | Allocate `nbytes` zeroed bytes; `NULL` on failure or non-positive size. |
| `Free(ptr)` | `nil` | Release a pointer from `MAlloc`. |
| `PeekU8(ptr, i)` / `PokeU8(ptr, i, v)` | `I64` / `v` | Read / write the `i`-th byte. |
| `PeekI64(ptr, i)` / `PokeI64(ptr, i, v)` | `I64` / `v` | Read / write the `i`-th 8-byte signed integer slot. |
| `PeekF64(ptr, i)` / `PokeF64(ptr, i, v)` | `F64` / `v` | Read / write the `i`-th 8-byte float slot. |
| `PeekPtr(ptr, i)` / `PokePtr(ptr, i, v)` | `Ptr` / `v` | Read / write the `i`-th pointer slot. A null pointer reads back as `NULL`. |

## Strings (native primitives)

HolyC keeps characters as integers, so `CharAt`/`Chr` bridge ints and 1-char
strings. The richer string helpers (`Str*`) build on these — see the stdlib
section.

| Function | Returns | Description |
|---|---|---|
| `CharAt(s, i)` | `I64` | Byte/char code at index `i`, or `-1` if out of range. |
| `Chr(code)` | `U8` | A one-character string for the given char code. |
| `Substr(s, start, len)` | `U8` | Substring, with `start` and `len` clamped to bounds. |
| `IndexOf(s, sub)` | `I64` | First index of `sub` in `s`, or `-1`. |
| `ToInt(x)` | `I64` | Parse a string (base 10), truncate a float, or pass an int through. |
| `ToStr(x)` | `U8` | Render any value as a string (matches print formatting). |

## File I/O (native primitives)

| Function | Returns | Description |
|---|---|---|
| `ReadFile(path)` | `U8` | Whole file as a string, or `NULL` if it cannot be opened. |
| `WriteFile(path, contents)` | `Bool` | Write a string (truncating the file); `TRUE` on success. |
| `AppendFile(path, contents)` | `Bool` | Append a string to a file; `TRUE` on success. |
| `FileExists(path)` | `Bool` | `TRUE` if the path can be opened for reading. |

Line-oriented wrappers (`ReadLines`, `WriteLines`, `AppendLine`) live in
[`std/io.bk`](#stdiobk--line-oriented-file-io).

## Math

`Abs`/`Min`/`Max` preserve int-vs-float; `Sqrt`/`Pow`/`Floor`/`Ceil` always
return `F64`.

| Function | Returns | Description |
|---|---|---|
| `Abs(x)` | `I64`/`F64` | Absolute value, preserving the numeric kind. |
| `Min(a, b)` | `U0` | The smaller of two numbers. |
| `Max(a, b)` | `U0` | The larger of two numbers. |
| `Sqrt(x)` | `F64` | Square root. |
| `Pow(base, exp)` | `F64` | `base` raised to `exp`. |
| `Floor(x)` | `F64` | Round toward negative infinity. |
| `Ceil(x)` | `F64` | Round toward positive infinity. |

## Scripting / OS

Thin, `NULL`-on-failure wrappers over POSIX (macOS + Linux) that make brokm
usable as a shell-script replacement.

| Function | Returns | Description |
|---|---|---|
| `Args()` | `U0[]` | A new array of the script's command-line arguments (those after the script path; for an AOT-compiled program, those after the program). |
| `Env(name)` | `U8` | The environment variable's value, or `NULL` if unset. |
| `Exit(code)` | *(never returns)* | Terminate the process immediately with the given status (flushes stdout/stderr). |
| `Shell(cmd)` | `I64` | Run a shell command; returns its exit status (`-1` on failure to launch). |
| `ShellStr(cmd)` | `U8` | Run a command and return its stdout as a string (including any trailing newline), or `NULL` if it could not start. |
| `Input()` | `U8` | One line from stdin without the trailing newline, or `NULL` on EOF. |
| `Time()` | `I64` | Seconds since the Unix epoch. |
| `TimeMs()` | `I64` | Milliseconds since the Unix epoch (wall clock). |
| `Sleep(ms)` | `nil` | Pause for `ms` milliseconds. |

Higher-level OS helpers (`EnvOr`, `ShellLines`, `ShellOk`) live in
[`std/os.bk`](#stdosbk--process--environment).

## Self-hosting & bytecode introspection

These let a brokm program act as a back-end for this very VM — the self-hosting
compiler in `examples/` uses them to emit and run real bytecode.

| Function | Returns | Description |
|---|---|---|
| `Opcode(name)` | `I64` | Resolve an opcode mnemonic (e.g. `"ADD"`, `"RETURN"`) to its integer `OpCode`, or `-1`. The `chunk.h` enum is the single source of truth. |
| `Assemble(code, consts [, arity])` | function | Turn a byte array (`code`) plus a constant-pool array (`consts`) into a callable function. Optional `arity` (default `0`) sets the parameter count. Rejects pools > 256 (one-byte operand limit) and code bytes outside `0..255`. |
| `MakeClass(name, fields)` | class | Build a class value with the given name and declared field-name array, so brokm can define structs at runtime. |
| `AddMethod(klass, name, fn)` | the class | Bind `fn` as a method named `name` on the class (invoked via `OP_INVOKE`; the receiver is `this` in local slot 0). |
| `ChunkCode(fn)` | `U0[]` | A new array of the function's bytecode bytes, as ints. |
| `ChunkConsts(fn)` | `U0[]` | A new array of the function's constant-pool values, in pool order (objects by reference). |
| `ValueDesc(v)` | `U8` | A short structural description of a value: its kind, plus the shape of a function (`"fn:name/arity"`) or class (`"class:Name(field,...);methods=N"`). |

---

# Standard library (`lib/std/`)

Written in brokm. `#include "std/std.bk"` pulls in all five modules.

## `std/str.bk` — string utilities

Every public name is prefixed `Str`. Builds only on the native string primitives
`CharAt`/`Chr`/`Substr`/`IndexOf`/`Len`.

| Function | Returns | Description |
|---|---|---|
| `StrIsSpace(c)` | `Bool` | Is the char code a space, tab, CR, or LF? |
| `StrContains(s, sub)` | `Bool` | Does `s` contain `sub`? |
| `StrStartsWith(s, prefix)` | `Bool` | Does `s` start with `prefix`? |
| `StrEndsWith(s, suffix)` | `Bool` | Does `s` end with `suffix`? |
| `StrIndexOfFrom(s, sub, from)` | `I64` | First index of `sub` at or after `from`, or `-1`. |
| `StrTrimLeft(s)` | `U8` | Drop leading whitespace. |
| `StrTrimRight(s)` | `U8` | Drop trailing whitespace. |
| `StrTrim(s)` | `U8` | Drop leading and trailing whitespace. |
| `StrSplit(s, sep)` | `U0[]` | Split `s` on every occurrence of `sep`. An empty separator returns `[s]`. |
| `StrJoin(parts, sep)` | `U8` | Concatenate parts with `sep` between them. Non-string elements are rendered with `ToStr`. |
| `StrReplace(s, from, to)` | `U8` | Replace every occurrence of `from` with `to`. |
| `StrRepeat(s, n)` | `U8` | `s` concatenated `n` times. |
| `StrPadLeft(s, width, pad)` | `U8` | Left-pad with `pad` until at least `width` long. |
| `StrPadRight(s, width, pad)` | `U8` | Right-pad with `pad` until at least `width` long. |
| `StrCmp(a, b)` | `I64` | Lexicographic byte comparison: `-1`, `0`, or `1`. (The VM's `<`/`>` are numeric-only.) |
| `StrUpper(s)` | `U8` | ASCII uppercase. |
| `StrLower(s)` | `U8` | ASCII lowercase. |

## `std/arr.bk` — array utilities

Prefixed `Arr`. Functions that reorder elements return a **new** array and leave
the argument untouched. Element comparisons use `==`. Includes `std/str.bk` for
`ArrSortStr`.

| Function | Returns | Description |
|---|---|---|
| `ArrIndexOf(xs, v)` | `I64` | Index of the first element equal to `v`, or `-1`. |
| `ArrContains(xs, v)` | `Bool` | Whether `v` is present. |
| `ArrSum(xs)` | `I64` | Sum of numeric elements (from 0). |
| `ArrMin(xs)` | `U0` | Smallest element; `NULL` when empty. |
| `ArrMax(xs)` | `U0` | Largest element; `NULL` when empty. |
| `ArrSlice(xs, start, len)` | `U0[]` | A new array of up to `len` elements from `start`, clamped to bounds. |
| `ArrCopy(xs)` | `U0[]` | A new shallow copy. |
| `ArrReverse(xs)` | `U0[]` | A new array with the elements reversed. |
| `ArrSortStr(xs)` | `U0[]` | A new array of strings sorted ascending by `StrCmp`. |
| `ArrSort(xs)` | `U0[]` | A new **numerically** sorted array (insertion sort). Use `ArrSortStr` for strings. |

## `std/io.bk` — line-oriented file I/O

Builds on the native `ReadFile`/`WriteFile`/`AppendFile` and `std/str.bk`.

| Function | Returns | Description |
|---|---|---|
| `ReadLines(path)` | `U0[]` | The file's lines as an array (no newline chars), or `NULL` if unreadable. A trailing final newline does not produce an empty last element. |
| `WriteLines(path, lines)` | `Bool` | Write the lines joined with `"\n"` plus a final newline; `TRUE` on success. |
| `AppendLine(path, line)` | `Bool` | Append one line (with newline); `TRUE` on success. |

> `ArrLinesDropLast(lines)` is an internal helper (returns the lines minus a
> final empty element) and not part of the public surface.

## `std/path.bk` — file-path helpers

Pure string manipulation on `/`-separated paths (macOS + Linux).

| Function | Returns | Description |
|---|---|---|
| `PathJoin(a, b)` | `U8` | Join two segments with exactly one `/`. An absolute `b` wins outright. |
| `PathDir(p)` | `U8` | The directory portion (`"."` if none, `"/"` for root entries). |
| `PathBase(p)` | `U8` | The final component. |
| `PathExt(p)` | `U8` | The extension including the dot (`""` if none). Dotfiles like `.profile` have no extension. |

> `PathLastSlash(p)` is an internal helper (index of the last `/`, or `-1`).

## `std/os.bk` — process & environment

Builds on the native `Env`/`Shell`/`ShellStr`/`Args` and `std/str.bk`.

| Function | Returns | Description |
|---|---|---|
| `EnvOr(name, fallback)` | `U8` | The environment variable, or `fallback` if unset. |
| `ShellLines(cmd)` | `U0[]` | The command's stdout as an array of lines (no newlines), or `NULL` if it could not start. |
| `ShellOk(cmd)` | `Bool` | `TRUE` when the command exits `0`. |
