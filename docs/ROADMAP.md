# brokm roadmap

Goals: **small, fast, robust, portable, easy to embed.** Each milestone ends only when the
build is clean (`-Wall -Wextra`, 0 warnings) and its tests pass.

## v0.1 — Runnable bytecode VM ✅ (done)

HolyC-flavored language on a stack VM: lexer → parser/AST → bytecode compiler → VM.
Arithmetic, variables (global + local), `if/else/while/for/do-while/switch`, functions +
recursion, bitwise/shift, HolyC-style `"...";` printing + `Print`. Golden test suite green.
No GC yet — heap objects are freed at exit via the intrusive object list. Object headers,
the `reallocate()` chokepoint, the AST seam, and macro-based `Value` access are all in place
for what follows.

## v0.2 — Garbage collection: mark-sweep ✅ (done)

Precise tri-color mark-sweep collector (`gc.c`):

- Collection is triggered inside `reallocate()` via a `bytesAllocated`/`nextGC`
  growth heuristic (grow factor 2, 1 MiB floor).
- Roots: value stack, call frames, and the globals table. The interned-string
  table is a **weak** set, pruned before sweep.
- Marking blackens function constants/names and native names; sweep walks the
  intrusive object list and frees the white objects.
- Collection is gated by `vm.gcEnabled` so it never runs during compilation,
  when the compiler holds objects not yet reachable from VM roots.
- Runtime allocation to exercise it: **string concatenation** (`"a" + "b"`).
- `make test-gc` runs the whole suite with `-DBK_DEBUG_STRESS_GC` (collect on
  every allocation); `-DBK_DEBUG_LOG_GC` logs each collection. All tests pass
  under stress, proving no live object is wrongly swept.
- `GcCollect()` native forces a collection.

## v0.3 — Generational GC + manual-memory mode

- Young copying generation + old mark-sweep; promotion on survival; a write barrier for
  old→young references (the `gen` header field is reserved for this).
- Opt-in **manual memory** for low-level code: `MAlloc` / `Free` and region/arena allocation
  that bypass the collector, selectable per allocation or per scope.

## v0.4 — Static type checking

- Use the AST + HolyC type annotations (`U8`…`I64`, `F64`, `Bool`, `U0`) for real checks and
  explicit coercions; emit typed bytecode (specialized int/float ops).
- This also fixes the v0.1 postfix `++`/`--` and switch-scope simplifications.

## v0.5 — Baseline JIT

- Compile hot functions to native code into `mmap`'d executable pages (arm64 first, then x64),
  with a VM fallback. Profile-gated; benchmark-driven.

## v0.6 — Embedding, stdlib, polish

- Richer `brokm.h`: register native functions, push/pop/convert values, multi-instance VMs.
- A small standard library (math, strings, I/O).
- Finalized docs and a stable language spec.

## Language features tracked across milestones

- Structs / classes, pointers, arrays (with v0.3 memory work).
- Multiple declarators per statement; command-style function calls; HolyC sub-switches.
- Modules / multi-file programs.
