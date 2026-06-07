# brokm

**brokm** — short for *"broken minded"* — is a small, fast, embeddable general-purpose
language with a clean [HolyC](https://en.wikipedia.org/wiki/TempleOS#HolyC)-flavored syntax.
It is built primarily for its author's own use, with four design goals: **small, fast, robust,
and easy to embed**.

This is **v0.10**: a working bytecode VM with a precise, **generational** mark-sweep garbage
collector, aggregate types (arrays + classes/structs **with methods**), an opt-in
**manual-memory** mode for low-level work, a **static type checker** that validates the HolyC type
annotations before any code runs, **typed bytecode** that specializes the int hot path, a
**baseline JIT** that compiles hot functions to native code (**arm64 + x86-64**, with a full
interpreter fallback), a **standard library** (file I/O, strings, math), **string-keyed maps**,
a **C embedding API** (register natives, exchange values, call brokm from C), and **multi-file
programs** via `#include` — enough that a [compiler written in brokm](examples/realcc.bk) emits
the VM's real bytecode and now **compiles its own source** (`make test-bootstrap`, byte-identical
to the C compiler — see [docs/ROADMAP.md](docs/ROADMAP.md)).

```holyc
// hello.bk — top-level code runs; a bare string statement prints.
"Hello World\n";

I64 Fib(I64 n)
{
  if (n < 2) return n;
  return Fib(n - 1) + Fib(n - 2);
}

I64 i = 0;
for (i = 0; i <= 10; ++i)
  "Fib(%d) = %d\n", i, Fib(i);
```

## Build & run

Requires a C99 compiler and `make` (no external dependencies).

```sh
make              # builds ./brokm  (-std=c99 -Wall -Wextra, 0 warnings)
./brokm file.bk   # run a program
./brokm           # start the REPL
make test         # run the golden test suite
make test-gc      # run the suite with the GC firing on every allocation
make bench        # benchmark the JIT against the interpreter (Fib)
make debug        # ASan/UBSan + bytecode/exec tracing build
```

## Language at a glance

- **Types** (HolyC-style): `U0 U8 U16 U32 U64 I8 I16 I32 I64 F64 Bool`; default integer is `I64`.
- **Type-first declarations**: `I64 x = 5;`  `F64 r = 3.14;`
- **Top-level code executes** — no `main()` required.
- **Functions**: `I64 Add(I64 a, I64 b) { return a + b; }`
- **Printing**: a bare string statement prints, with printf-style args:
  `"x = %d\n", x;` — or call `Print(...)`.
- **Arrays**: dynamic, heap-allocated — `I64[] a = [1, 2, 3]; a[0] = 9;` with `Len`/`Append`.
- **Classes/structs**: `class Point { I64 x; I64 y; }`, `Point p = Point(3, 4); p.x = 9;`
  (reference semantics).
- **Methods**: declare `I64 Area() { return this.w * this.h; }` in the class body and call
  `r.Area()`; the receiver is `this`.
- **Manual memory**: `U0 b = MAlloc(32); PokeI64(b, 0, 42); Free(b);` — raw, GC-invisible
  buffers with typed peek/poke, plus `GcDisable`/`GcEnable`.
- **Maps**: string-keyed hash maps — `U0 m = MapNew(); MapSet(m, "k", 1); MapGet(m, "k");`
  with `MapHas`/`MapDelete`/`MapLen`/`MapKeys`.
- **Control flow**: `if/else`, `while`, `for`, `do/while`, `switch/case/default`,
  `break`, `continue`, `return`.
- **Multi-file**: `#include "lib.bk"` — textual, include-once, resolved relative to the file.
- **Operators**: `+ - * / %`, comparisons, `&& || !`, bitwise `& | ^ ~ << >>`,
  assignment + compound (`+=` …), `++ --`.

Full reference: [docs/SYNTAX.md](docs/SYNTAX.md).
Internals: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Embedding

```c
#include "brokm.h"

static BrokmValue HostAdd(int argc, const BrokmValue *args) {
  return brokm_int(brokm_as_int(args[0]) + brokm_as_int(args[1]));
}

int main(void) {
  brokm_init();
  brokm_register("HostAdd", HostAdd);              /* expose a C native */
  brokm_eval("I64 Triple(I64 n) { return HostAdd(n, HostAdd(n, n)); }");

  BrokmValue args[1] = { brokm_int(7) }, result;
  brokm_call("Triple", 1, args, &result);          /* call brokm from C */
  /* result == 21; also: brokm_get_global / brokm_set_global, string exchange */

  brokm_shutdown();
  return 0;
}
```

The v0.9 API lets a host **register native functions**, **exchange values** (ints, floats, bools,
strings), **read/set globals**, and **call brokm functions from C**. See
[`examples/embed.c`](examples/embed.c) for a complete program (`make embed && ./embed-demo`).

## Editor support

Syntax highlighting for `.bk` files ships under [`editors/`](editors/):

- **Neovim / Vim** — drop-in regex syntax (`editors/nvim/`), no build step.
- **Sublime Text** — a `.sublime-syntax` (`editors/sublime/`), no build step.
- **Zed** — a Tree-sitter extension (`editors/zed/`).
- **Tree-sitter grammar** (`editors/tree-sitter-brokm/`) powers Zed and the
  `nvim-treesitter` option; it parses every `.bk` file in this repo with no errors.

Install instructions for each editor are in [`editors/README.md`](editors/README.md).

## Status

brokm runs real programs (arithmetic, variables, control flow, functions, recursion, strings,
**dynamic arrays**, **classes/structs**, **manual memory**, printing) on a stack bytecode VM,
with a precise **generational** mark-sweep collector (minor + major, young→old promotion) whose
write barrier is exercised by array and field mutation and verified red/green. A **static type
checker** validates the type annotations between parse and compile — catching arity, argument,
field, and class-type errors before execution — using gradual typing so existing programs are
unaffected. The compiler emits **typed bytecode**: int-specialized arithmetic and comparison
opcodes on the hot path, with a runtime guard that deopts to the generic handler so gradual
typing stays correct. A **baseline JIT** (macOS, **arm64 + x86-64**) compiles hot functions to
native code in `mmap`'d executable pages — profile-gated, with inlined integer
arithmetic/comparisons, branches, and recursion, deopt guards for correctness, and a full
interpreter fallback for ineligible functions and every other platform. It runs the recursive
`Fib` benchmark ~2× faster than the interpreter on each architecture (`make bench`). A **standard
library** of native builtins covers file I/O (`ReadFile`/`WriteFile`/`PrintErr`), strings
(`CharAt`/`Chr`/`Substr`/`IndexOf`/`ToInt`/`ToStr`), and math
(`Abs`/`Min`/`Max`/`Sqrt`/`Pow`/`Floor`/`Ceil`) — enough that `examples/lexer.bk` tokenizes and
`examples/calc.bk` parses + evaluates brokm-flavored source written in brokm, the first steps
toward self-hosting. **String-keyed maps** (`MapNew`/`MapGet`/`MapSet`/…) add the symbol-table
primitive a self-hosted compiler needs, with their mutations exercising the write barrier
red/green. Classes carry **methods** (`obj.m(args)` with an implicit `this`, dispatched through a
new `OP_INVOKE`), so a compiler's data types can hold behavior. A **C embedding API** lets a host
register native functions, exchange scalar and string values, read/set globals, and call brokm
functions from C (`examples/embed.c`). **`#include`** splits a program across files — textual,
include-once, resolved relative to each file. Putting it together, **`examples/realcc.bk` is a
compiler written in brokm** (lexer → parser → code generator) that emits the C VM's **real
bytecode** and runs it directly — and it now **compiles its own complete source**: the
self-compiled compiler produces byte-identical output to the C compiler (`make test-bootstrap`).
Next up: a runtime in brokm and multi-instance VMs — see the roadmap.

## License

TBD by the author.
