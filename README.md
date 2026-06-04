# brokm

**brokm** — short for *"broken minded"* — is a small, fast, embeddable general-purpose
language with a clean [HolyC](https://en.wikipedia.org/wiki/TempleOS#HolyC)-flavored syntax.
It is built primarily for its author's own use, with four design goals: **small, fast, robust,
and easy to embed**.

This is **v0.3**: a working bytecode VM with a precise, **generational** mark-sweep garbage
collector. A JIT compiler, aggregate types (arrays/structs), and an opt-in manual-memory mode are
on the roadmap — the engine is structured so they slot in without rewrites
(see [docs/ROADMAP.md](docs/ROADMAP.md)).

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
- **Control flow**: `if/else`, `while`, `for`, `do/while`, `switch/case/default`,
  `break`, `continue`, `return`.
- **Operators**: `+ - * / %`, comparisons, `&& || !`, bitwise `& | ^ ~ << >>`,
  assignment + compound (`+=` …), `++ --`.

Full reference: [docs/SYNTAX.md](docs/SYNTAX.md).
Internals: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Embedding

```c
#include "brokm.h"

int main(void) {
  brokm_init();
  brokm_eval("\"embedded!\\n\";");
  brokm_shutdown();
  return 0;
}
```

## Status

v0.3.5 runs real programs (arithmetic, variables, control flow, functions, recursion, strings,
**dynamic arrays**, **classes/structs**, printing) on a stack bytecode VM, with a precise
**generational** mark-sweep collector (minor + major, young→old promotion) whose write barrier is
exercised by array and field mutation and verified red/green. Next up: pointers + the opt-in
manual-memory mode — see the roadmap.

## License

TBD by the author.
