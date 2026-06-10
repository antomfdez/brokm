# CLAUDE.md — working on brokm

brokm is a small HolyC-flavored language: bytecode VM + generational GC +
static-then-gradual type checker + baseline JIT (arm64/x86-64) + AOT
compile-to-C, written in C99 with zero dependencies. The whole runtime is
`src/`; the standard library written in brokm itself is `lib/std/`.
Per-module map: `docs/CODEMAP.md`. Design history: `docs/ROADMAP.md`.

## Build & verify

```sh
make                  # release build, must stay 0 warnings
make test             # golden suite: tests/cases/*.bk vs *.expected
```

A change is "verified" only after the FULL matrix passes (CI runs all of it):

```sh
make test                                  # interpreter
BROKM_JIT_THRESHOLD=1 make test            # force-JIT every function
make CFLAGS="-std=c99 -Wall -Wextra -O2 -Iinclude -Isrc -DBK_NO_JIT" && make test
make test-gc                               # GC fires on every allocation
make test-aot                              # every test AOT-compiled to native
make test-embed test-selfcompile test-bootstrap test-fixpoint
```

All four execution paths (interpreter / JIT / AOT / GC-stress) must produce
byte-identical output. `make CFLAGS=...` overrides drop the `+=` flags, so
always repeat `-Iinclude -Isrc` in an override.

## Invariants that bite (learned the hard way)

- **Write barrier**: any store of a possibly-young object into a possibly-old
  one needs `gc_write_barrier(owner, value)`. "Rooted" is NOT "safe": rooting
  an object across an allocation lets a minor GC PROMOTE it to old, and a
  subsequent raw store of a fresh young object into it is an unbarriered
  old→young edge (the v0.11.1 Linux segfault class). macOS malloc masks
  use-after-free; glibc doesn't. Verify barrier changes red/green with
  `-DBK_NO_WRITE_BARRIER`.
- **GC-safety in natives**: every `array_append`/`string_copy`/allocation can
  collect. Root fresh objects with `vm_push`/`vm_pop` until they are reachable
  (see `native_args` / `native_map_keys` for the pattern).
- **Gradual typing**: a static `I64` slot can hold a float at runtime. Typed
  fast paths (OP_I*, JIT, AOT) must GUARD on runtime tags and deopt to the
  generic helper. Never trust a static type at runtime.
- **One semantics, three tiers**: interpreter, JIT, and AOT share the
  `jit_h_*` helper ABI declared in `jit.h`. New opcodes/behavior go into a
  helper all three call — never reimplement semantics per tier.
- **New native = two registrations**: `vm_define_native` in
  `src/natives.c` AND the `NAMES[]` list in `src/typecheck.c`, or the type
  checker rejects calls to it.
- **Version bump** (`BROKM_VERSION` in `src/api.c`): regenerate
  `examples/embed.expected` (its first line prints the version) or
  `test-embed` fails.
- **Bytecode operands**: constant indexes are 1 byte. The C compiler dedupes
  constants (`make_constant`, strict identity — int 3 ≠ float 3.0) and
  `Assemble` refuses pools > 256; keep both guards when touching them.
- **ASan**: hangs at startup in the macOS sandbox; works fine in a Linux
  container (`podman run --rm -v "$PWD":/src -w /src docker.io/library/gcc:14 ...`).
  That is the debug recipe for GC bugs.

## Conventions

- C99, `-Wall -Wextra`, zero warnings; files ≤ ~800 lines (split when over).
- Golden tests: add `tests/cases/x.bk` + `x.expected` (stderr is merged via
  `2>&1`; a `.bk` without `.expected` is skipped — used for include-only
  helpers). Every bug fix gets a red/green-verified regression test.
- The stdlib search path: `#include "std/..."` resolves relative to the
  including file, then `$BROKM_HOME/lib`, then `~/.brokm/lib`. The test
  runners export `BROKM_HOME` = repo root.
- lib/std modules prefix their public names by module (Str*, Arr*, Path*…)
  because brokm has one flat namespace.
- Commit style: `<type>: <description>` (feat/fix/refactor/docs/test/chore/
  perf/ci). No Co-Authored-By trailers.
