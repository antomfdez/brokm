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

## v0.3 — Generational GC ✅ (done)

**Generational mark-sweep** (`gc.c`), non-moving:

- Two generations via the `gen` header field: young (0) and old (1). Objects start young;
  survivors of a minor collection are promoted to old.
- `collect_garbage(major)`: a **minor** collection reclaims only young garbage and leaves old
  objects untouched; a **major** collection is a full heap mark-sweep.
- Triggers: minor when growth since the last minor crosses `minorThreshold` (256 KiB); major
  when live bytes cross `nextGC`. Stress mode hammers minor every allocation + major every 4th.
- `GcCollect()` forces a major collection; `GcMinor()` forces a minor one.
- A **write barrier** (`gc_write_barrier`) + remembered set are in place for old→young edges.
  They are correctly inert today: brokm has no mutable aggregate objects, so no old object is
  ever made to point at a young one, and every young reference originates from a root that minor
  GC already scans. They activate when arrays/structs land.

**Why not a copying young space:** moving objects would require precisely updating every pointer
(stack, frames, constants, native C-locals); with no mutable aggregates there is nothing to gain
yet, so the non-moving design is kept.

**Manual-memory mode is deferred** to follow aggregate/pointer support. `MAlloc`/`Free` and
region/arena allocation are only meaningful once the language can hold and dereference
pointers/arrays — without that there are no call sites to manage. The same prerequisite
(mutable aggregates) is what makes the write barrier above non-trivial. See the milestone below.

## v0.3.5 — Aggregates: arrays ✅ · classes/structs ✅

**Dynamic arrays ✅.** Heap `ObjArray` holding any values; literal `[a, b, c]`, indexing `a[i]`
(get/set), nesting, and `Len`/`Append` builtins (`OP_ARRAY`, `OP_INDEX_GET`, `OP_INDEX_SET`).

**Classes/structs ✅.** `class`/`struct` reference-semantics objects (`ObjClass`, `ObjInstance`)
with typed fields; construction by calling the class (`Point(3, 4)`), `Point p;` default
construction, and `p.x` field get/set (`OP_GET_FIELD`, `OP_SET_FIELD`). The parser recognizes
class names as types via one-token lookahead.

Both made the generational write barrier **load-bearing and verified**: the minor collector
skips the old generation entirely, so a young value reachable only through an old array element
or an old instance field survives only because `gc_write_barrier` recorded the edge.
`tests/cases/gc_barrier.bk` (array) and `gc_field_barrier.bk` (field) prove it red/green —
building with `-DBK_NO_WRITE_BARRIER` makes them fail; the default build keeps the values alive.

## v0.3.6 — Manual-memory mode ✅ (done)

Raw, GC-invisible memory for low-level work:

- A `VAL_PTR` value type the collector never tracks or follows, allocated with `MAlloc(nbytes)`
  and released with `Free(ptr)` (a null pointer surfaces as `NULL`/nil).
- Typed element access: `PeekU8/PokeU8`, `PeekI64/PokeI64`, `PeekF64/PokeF64`,
  `PeekPtr/PokePtr` — enough to hand-build buffers and linked structures (see
  `examples/manual.bk`).
- `GcDisable()` / `GcEnable()` bracket no-collection sections for deterministic, pause-free code.

This is the HolyC-style escape hatch: drop to manual buffers where you need control, while the
rest of the program stays garbage-collected. Address-of-variable (`&`/`*` on managed values) is
intentionally omitted — taking raw addresses of GC-managed values fights the collector; raw
pointers come only from `MAlloc`. (Region/arena allocation could layer on later if needed.)

## v0.4 — Static type checking ✅ (done)

A **type-checking pass** (`typecheck.c`) runs over the AST between parse and bytecode
compilation. The HolyC type annotations — previously parsed and discarded — are now carried
through the tree (`Type` on every `Expr`; declared/return/param/field types on the relevant
statements) and used for real checks, reported with line numbers before any code runs:

- **Calls**: a user function is checked for exact arity and argument-type compatibility;
  a constructor call (`Point(...)`) is checked against the class's declared fields (at most
  one argument per field, missing fields default to nil, matching the VM).
- **Fields**: `obj.x` get/set on a class instance must name a declared field of that class.
- **Aggregates vs scalars**: indexing requires an array/string; `+` requires numbers or
  strings; `- * / %` and bitwise/shift require numerics/integers; instances cannot be added,
  indexed, or assigned across class types.
- **Assignment/initialization**: a class-typed slot only accepts an instance of that class
  (or `nil`).

**Gradual by design.** brokm's scalar type keywords are weak storage hints — idiomatic code
keeps strings and pointers in `U8` slots and uses `U0` as a generic/void type — so scalars,
strings, pointers, and arrays interconvert freely and `U0` maps to a gradual "unknown" that is
compatible with everything. Strict identity is enforced only where brokm has real type
identity: **class instances and class values**. Anything the checker cannot pin down is
`TY_UNKNOWN` and never wrongly rejected, so all existing programs still compile. Verified
red/green: `tests/cases/type_err_{arg,field,assign}.bk` are rejected statically (bypassing the
pass lets the same errors fall through to runtime), and `tests/cases/typecheck.bk` confirms a
well-typed program still runs and prints correctly.

**Deferred:**
- **Separate fix** — the v0.1 postfix `++`/`--` value semantics and switch-scope
  simplification (independent of typing).

## v0.4.1 — Typed bytecode ✅ (done)

The bytecode compiler now uses the `Expr` types the checker records to emit **int-specialized
opcodes** on the arithmetic/comparison hot path: when both operands are statically `TY_INT`,
`OP_IADD OP_ISUB OP_IMUL OP_IDIV OP_IMOD` and `OP_ILESS OP_ILESS_EQUAL OP_IGREATER
OP_IGREATER_EQUAL` replace their generic forms (compiler.c, gated by `BK_NO_TYPED_OPS`). Each
runs a branch-lean integer path with no type-tag dispatch.

**Guarded with deopt.** Because brokm is gradually typed (`type_assignable(TY_INT, TY_FLOAT)` is
true — a float may occupy a statically-int slot, e.g. `I64 c = 10.0 / 4.0;`), static `TY_INT` is
a hint, not a runtime guarantee. So every specialized op carries a runtime `IS_INT` guard and
**deopts** — `goto`s its generic counterpart — on a miss, preserving exact semantics (string
concat for `+`, divide-by-zero error, int→float promotion). The guard is load-bearing, verified
red/green: dropping the `IS_INT` check makes the float-in-int-slot case in
`tests/cases/typed_ops.bk` print garbage; with it, `3.5`. The default and `-DBK_NO_TYPED_OPS`
builds produce byte-identical output. Equality (`== !=`, any-value) and bitwise/shift (already
integer-only generic paths) are not specialized — an easy future extension.

## v0.5 — Baseline JIT ✅ (done, arm64 + x86-64 / macOS; Linux added in v0.11.1)

Hot functions are compiled to native code in `mmap`'d executable pages
(`MAP_JIT` + `pthread_jit_write_protect_np`), profile-gated, with a full interpreter fallback.
Two backends share one driver: `jit.c` owns the page pool, profiling, and the
`jit_init`/`jit_try_compile` dispatch, while `jit_arm64.c` (Apple Silicon) and `jit_x64.c`
(Intel, incl. Apple via Rosetta) provide the encoders + bytecode walker through the
`jit_compile_arch`/`jit_selftest_arch` hooks. On any other platform, or with `-DBK_NO_JIT`, every
function runs on the interpreter.

- **Profiling.** `ObjFunction` carries `callCount`/`nativeCode`/`jitDisabled`. A function is
  compiled once its call count crosses a threshold (env `BROKM_JIT_THRESHOLD`, default 50; tests
  set `1`). A function the compiler bails on is marked disabled and never retried.
- **Faithful stack mirror.** Generated code runs on the *same* VM value stack as the
  interpreter, op-for-op, so it is correct by construction. Integer arithmetic, comparisons, and
  control flow (`if`/loops/recursion) are inlined as native instructions; two callee-saved
  registers hold the frame base and a cached `vm.stackTop` (`x19`/`x20` on arm64, `r14`/`r15` on
  x64). Everything else — globals, calls, `print`, equality — calls back into the interpreter's
  own C helpers (`jit_h_*` in vm.c). Calls re-enter through `call_value`; native↔native recursion
  (e.g. `Fib`) stays in native+C.
- **Guarded fast path.** Because brokm is gradually typed, an int-typed slot may hold a float at
  runtime, so each inlined op checks the operand tags and **deopts** to the C helper on a miss
  (and on divide-by-zero), preserving exact semantics.
- **Eligibility.** Functions touching aggregates/objects (arrays, indexing, fields), or
  bitwise/unary ops, are ineligible and run on the interpreter — graceful fallback, verified by
  the array/struct tests still passing.
- **Encoder self-test.** Each backend validates its encoders at startup (`jit_init`) by JIT-ing
  and running known routines; a failure disables the JIT (interpreter only) rather than
  miscompiling.
- **Verified — both backends.** Output is byte-identical to the interpreter across the whole
  suite three ways (default, forced `BROKM_JIT_THRESHOLD=1`, and `-DBK_NO_JIT`) and under GC
  stress, on **arm64 natively** and on **x86-64** (built with `make CC="clang -arch x86_64"`, run
  under Rosetta). `tests/cases/jit.bk` covers recursion, loops, comparisons, and int div/mod.
  **Speedup:** `make bench` (Fib(34)) — arm64 ~2× (≈303→153 ms); x86-64 ~2.4× (≈551→231 ms under
  Rosetta), each vs its own `-DBK_NO_JIT` build.

**Deferred (v0.5.x):** a **Linux** path — **done in v0.11.1** (plain RWX mmap, no `MAP_JIT`;
`__builtin___clear_cache` instead of `sys_icache_invalidate`); JIT'd frames in the error
backtrace (native frames currently omit their backtrace line); inlining bitwise/unary ops and
aggregates; keeping the cached stack-top in a register across calls.

## v0.6 — Standard library ✅ (done) · embedding/polish (in progress)

**Standard library ✅.** A set of native builtins (natives.c) covering the three areas a real
program — and a self-hosting compiler — needs:

- **File I/O:** `ReadFile(path)` → string (or nil), `WriteFile(path, str)` → `Bool`,
  `PrintErr(...)` (printf-style, to stderr).
- **Strings:** `CharAt(s, i)`, `Chr(code)`, `Substr(s, start, len)`, `IndexOf(s, sub)`,
  `ToInt(s)`, `ToStr(x)`. Equality already works (strings are interned, so `==` compares
  content).
- **Math:** `Abs`, `Min`, `Max` (int-vs-float preserving), `Sqrt`, `Pow`, `Floor`, `Ceil`.

String results go through the GC-safe `string_copy`/`string_take`; the builtins are registered
with the type checker as gradual `TY_UNKNOWN` and need no VM/JIT changes (native calls from JIT'd
code already route through `jit_h_call`). Verified byte-identical across the interpreter, forced
JIT, and `-DBK_NO_JIT`, and under GC stress (`tests/cases/stdlib.bk`, `io.bk`).

**Deferred to v0.6.x / v0.7:** a richer embedding API (register natives + exchange values from C)
— **done in v0.9, below**; **multi-instance VMs** (replacing the global `VM vm` — a cross-cutting
refactor), and a formal language spec.

## v0.7 — Maps ✅ (done)

A built-in **string-keyed hash map** (`ObjMap`), the symbol-table primitive self-hosting needs.
Structurally an `Obj` wrapping the existing string `Table` (`table.c`) — the same shape as an
`ObjInstance`'s field table, but with keys supplied at runtime — so the bytecode compiler, VM
opcodes, and JIT are unchanged (maps are reached only through native calls, which JIT'd code
already routes through `jit_h_call`).

- **API (natives):** `MapNew`, `MapSet(m, k, v)`, `MapGet(m, k)` (→ value or `NULL`),
  `MapHas`, `MapDelete`, `MapLen` (live entries, tombstones excluded), `MapKeys` (→ array).
- **GC.** Map keys and values are strong roots, marked like instance fields. `map_set` applies
  the generational **write barrier** for old-map → young-value edges — load-bearing and verified
  red/green by `tests/cases/map_barrier.bk` (it prints `bark` on the default build and nothing
  under `-DBK_NO_WRITE_BARRIER`), the same proof pattern as the array/field barrier tests.
- **Verified** byte-identical across the interpreter, forced JIT (`BROKM_JIT_THRESHOLD=1`), and
  `-DBK_NO_JIT`, and under GC stress (`tests/cases/map.bk`).

**Deferred:** map literal syntax (`{...}`), non-string (any-value) keys, and in-language
iteration sugar — each layers on without reworking this.

## v0.8 — Methods on classes ✅ (done)

Classes gained **instance methods** with an implicit receiver `this`, called as `obj.m(args)`.
Behavior that used to be written as free functions over instances can now live on the class.

- **Declaration.** A class body entry whose name is followed by `(` is a method (the leading
  type is its return type); otherwise it's a field. Methods are parsed into the class AST node
  and, because classes are built at compile time, compiled and stored straight into the class's
  **method table** — no runtime class-construction opcode.
- **Dispatch.** `obj.m(args)` compiles to a new **`OP_INVOKE`** (method-name constant + arg
  count): the receiver is pushed into the callee slot, so the method sees it as `this` (slot 0,
  named `this` by the compiler). A field holding a callable still shadows methods (field-first
  lookup), preserving `obj.fnField()`.
- **`this`.** Inside a method, `this.field` reads/writes the receiver and `this.other()` calls a
  sibling method. `this` is an ordinary local (slot 0), not a new keyword.
- **Gradual typing.** Member access on an instance now accepts method names as well as fields;
  accessing a name that is neither is a static error. Method bodies are checked with `this`
  bound to an instance of the class.
- **GC & JIT.** The class's method table is marked like instance fields (verified under
  `make test-gc`). `OP_INVOKE` and field access make a function JIT-ineligible, so methods run on
  the interpreter — output stays byte-identical across JIT modes.
- **Verified** four ways (default / forced JIT / `-DBK_NO_JIT` / GC stress), 28 tests, 0 warnings
  (`tests/cases/methods.bk`, `method_err.bk`).

**Deferred:** first-class/bound methods (storing `obj.m`), implicit-`this` sibling calls,
inheritance, static/class methods, and a `self` alias for `this`.

## v0.9 — Embedding API ✅ (done)

"Easy to embed" is a core goal; the public C API (`include/brokm.h`) now lets a host program drive
brokm with real values, not just `eval` strings.

- **Value exchange.** An opaque, by-value `BrokmValue` (a 16-byte handle, layout-compatible with
  the internal `Value`) with constructors (`brokm_int/float/bool/nil/string`) and
  predicates/accessors. `api.c` bridges the two through a `union` and a size assertion; scalars
  and strings cross the boundary (aggregates do not, yet).
- **Host natives.** `brokm_register(name, fn)` exposes a C function
  (`BrokmValue (*)(int, const BrokmValue *)`) to scripts. It registers through the existing
  `vm_define_native` and adds the name to the type checker's (now runtime-extensible) native set
  so scripts may call it.
- **Globals + calls.** `brokm_get_global` / `brokm_set_global`, and `brokm_call(name, argc, args,
  &result)` to invoke a brokm function from C (built on a new ungated `vm_invoke` that mirrors the
  JIT's call helper).
- **GC-safety.** Host-created strings are kept alive by a small C-API temp-root stack marked by
  the collector and cleared at each public-call boundary; verified by running the example under
  stress GC.
- **Worked example.** `examples/embed.c` (`make embed`, `make test-embed`) registers natives,
  evaluates source, calls brokm from C, exchanges scalars + strings, and reads/sets globals;
  byte-identical with and without the JIT.
- **Bug fixed along the way.** `OP_RETURN`'s top-level special case assumed the outermost frame is
  always the `<script>`; an embedded `brokm_call`'s callee is also outermost, so the result was
  discarded. Now gated on the nameless `<script>` function — the existing suite stays green.

**Deferred:** multi-instance VMs (replacing the global `VM vm`), exchanging
arrays/instances/maps across the boundary, and a formal spec.

## v0.10 — Modules / multi-file ✅ (done)

Programs can span files with **`#include "path"`**, in the HolyC spirit: textual, flat-namespace
inclusion, resolved **entirely in the lexer** so the parser, type checker, compiler, VM, and JIT
are unchanged.

- The lexer keeps a stack of source frames; on `#include "path"` it resolves the path **relative
  to the including file's directory**, reads the file, and pushes a frame — the parser sees one
  continuous token stream, so cross-file functions, classes (with methods), and ordering all work
  as if the files were concatenated, sharing one `classNames` registry.
- **Include-once / cycle-safe** by canonical path (`realpath`): re-including a file is a no-op, so
  mutually-including files terminate.
- A missing file or malformed directive is a **compile error** with a line number, not a crash.
- Included source buffers are freed after parsing (the AST holds interned strings, not source
  pointers). `brokm_run_file` resolves includes relative to the file's directory; `brokm_eval`
  (and the REPL) relative to the current directory.
- **Verified** four ways (default / forced JIT / `-DBK_NO_JIT` / GC stress) plus the embedding
  example — 29 tests, 0 warnings (`tests/cases/modules.bk` + the `mod_lib.bk` it includes).

**Deferred:** namespaced/qualified imports (`math.Sqrt`), per-module private globals, a module
search path, and selective imports.

## v0.11 — Multi-instance VMs ✅ (done)

The runtime is now **instance-based**: `brokm_new()` creates an independent VM — its own value
stack, globals, interned strings, object list, GC, and host-native registry — and `brokm_free()`
destroys it. A process holds any number of them, created, run (interleaved), and destroyed in any
order. This closes the embedding gap deferred at every milestone since v0.5.

- **Current-instance pointer, not parameter threading.** The single global `VM vm` became
  `VM *vm`, the *current* instance; the interpreter, GC, allocator, and natives act on it
  unchanged, so `object.c`/`natives.c` stay free of VM-pointer plumbing and the diff is almost
  entirely mechanical. Every public API entry point switches to its target VM and restores the
  previous one on exit, so nested cross-VM calls (a host native on VM A driving VM B) behave.
- **Instance-based public API.** `brokm.h` gained the opaque `BrokmVM`, `brokm_new`/`brokm_free`,
  and a `BrokmVM *` first parameter on every stateful call (`brokm_eval`, `brokm_run_file`,
  `brokm_register`, `brokm_call`, globals, `brokm_string`). Pure-bits value constructors
  (`brokm_int`…) stay VM-less; inside a host native, `brokm_current()` is the calling instance.
- **Per-VM host natives.** The type checker's host-native registry moved into the VM, so a
  native registered on one instance neither resolves nor typechecks on another.
- **JIT and instances.** Generated code bakes its *owning* VM's addresses (`&vm->stackTop`),
  which is sound because functions never migrate between instances. The executable-page pool is
  process-wide — every VM's code lives in it — so it is **reference-counted** by live VMs and
  released when the last one is destroyed; the encoder self-test runs once per process.
- **Verified.** The whole suite (50 tests) passes four ways (default / forced JIT / `-DBK_NO_JIT`
  / GC stress), 0 warnings, plus `test-selfcompile` and `test-bootstrap`. `examples/embed.c` now
  runs **two extra VMs side by side** — same global names with different values, per-VM native
  visibility, and continued use of the survivors after freeing one — byte-identical with the
  JIT, without it, and under stress GC.

**Scope note:** the runtime stays single-threaded — VMs interleave freely on one thread, but one
VM per *thread* would additionally need a thread-local current pointer and a per-VM (or locked)
JIT page pool.

## v0.11.1 — Portability + CI ✅ (done)

"Portable" was a stated goal that had only ever been verified on one machine. brokm now builds
and passes its **entire matrix on Linux** — both JIT backends included — and a **GitHub Actions
workflow** (`.github/workflows/ci.yml`) checks it on every push: macOS arm64 + Linux x86-64,
building with **`-Werror`**, running the suite four ways (default / forced JIT / `-DBK_NO_JIT` /
GC stress) plus the embedding, self-compile, and bootstrap checks.

- **Linux JIT.** `BK_JIT_ENABLED` now covers Linux: the page pool takes a plain RWX anonymous
  mapping (no `MAP_JIT`, no per-thread write-protect toggle) and flushes the instruction cache
  with `__builtin___clear_cache` (a no-op on x86-64, required on arm64). The arm64 and x86-64
  encoders/walkers needed **zero changes** — all platform specifics were already confined to the
  shared pool in `jit.c`.
- **glibc strictness.** Two small fixes: `realpath(3)` needs `_XOPEN_SOURCE` under `-std=c99`
  (lexer.c), and `MAP_ANONYMOUS` needs `_DEFAULT_SOURCE` (jit.c); gcc's `-Wformat-truncation`
  flagged the `#include`-path join buffer, now sized so the join can never truncate.
- **A real GC bug, caught by porting.** The Linux suite crashed under GC stress where macOS had
  always passed: `Assemble`'s `fn->name = string_copy("<asm>")` stores a fresh **young** string
  into a function that the interning allocation itself may have **promoted to old** — a raw
  old→young edge with no write barrier. The weakly-interned name was swept on the next minor
  collection and `fn->name` dangled; macOS malloc left the freed block intact (silently
  "working"), glibc reused it (segfault), and **AddressSanitizer on Linux** pinpointed it. One
  `gc_write_barrier` call fixes it — the same pattern every other old→young store site already
  used. The whole suite now runs **ASan-clean under stress GC** on Linux.
- **Verified.** macOS arm64 + x86-64 (Rosetta): full matrix. Linux arm64 + x86-64 (containers):
  full matrix with `-Werror`, plus the ASan/stress sweep. CI runs the same steps on every push.

## v0.11.2 — Bootstrap fixpoint + Assemble guard ✅ (done)

Two hardening items from the post-bootstrap list. The bootstrap test got its classic closure
check, and the one known silent-corruption path in the self-hosting bridge now fails loudly.

- **Three-stage fixpoint.** `make test-fixpoint` (and `tests/cases/selfhost20.bk` under the full
  four-way matrix): the C-hosted realcc compiles realcc's source (stage 1), the resulting
  self-compiled realcc compiles the same source again (stage 2), and the two compiled compilers
  must be **byte-for-byte identical** — code arrays, constant pools, and every nested function,
  compared recursively. Where `make test-bootstrap` only checks that two stages agree on
  `sample.bk`'s output, the fixpoint proves the self-compiled compiler reproduces *itself*.
  The comparison runs in brokm (`examples/fixpoint.bk`) on three new read-only introspection
  natives: `ChunkCode(fn)` (bytecode bytes), `ChunkConsts(fn)` (constant pool), and
  `ValueDesc(v)` (a structural tag — `fn:name/arity`, `class:Name(fields);methods=N`, or the
  value's kind).
- **`Assemble` guard.** A chunk's constant operand is one byte; realcc's `AddConst` dedups but
  never bounded the pool, so a function with >256 *unique* constants would wrap the byte and
  silently read the wrong constant — exactly the bug class that scrambled the first bootstrap
  attempt (step 22). `Assemble` now refuses a constant pool past 256 entries and any code byte
  outside 0..255, reporting on stderr and returning `NULL` (its existing bad-input convention),
  matching the C compiler's "Too many constants in one chunk" error (`tests/cases/asm_guard.bk`).
- **Verified.** 52 golden tests four ways (default / forced JIT / `-DBK_NO_JIT` `-Werror` /
  GC stress) + embed + selfcompile + bootstrap + fixpoint, 0 warnings; the comparator itself
  red/green-checked (it flags differing top-level constants and differing nested-function
  bodies). CI now runs `test-fixpoint` too.

## v0.12 — AOT compile-to-C + CLI subcommands ✅ (done)

The first cut of the path to the long-term vision: **programs that run without the VM toolchain
at the door** — and, much later, a kernel written in brokm (which can never carry a JIT, a
mandatory GC, or libc). `brokm build` translates a program to C and drives the system C compiler
to a standalone native executable.

- **CLI subcommands.** `brokm run file.bk`, `brokm build file.bk [-o out] [--emit=c] [--keep-c]
  [--cc <compiler>] [-O0..-O3] [--verbose] [--quiet]`, `brokm --help`. The legacy spellings
  (`brokm file.bk`, bare `brokm` for the REPL, `--version`) still work, so every existing test,
  script, and finger habit is untouched.
- **Bytecode → C, correct by construction.** The back-end (`src/cemit.c`) does for C source what
  the JIT does for machine code: each `ObjFunction` chunk becomes a C function with the `JitFn`
  signature (`bool bk_f<i>(Value *slots)`) whose body is the interpreter specialized for that
  chunk — straight-line statements on the VM value stack, `goto` labels at jump targets, guarded
  int fast paths for the typed opcodes, and the shared `jit_h_*` runtime helpers for everything
  else. The interpreter's own opcode cases now *delegate to those same helpers* (extracted in
  this milestone: define-global, negate, bit-not, array, index get/set, field get/set, invoke),
  so the three execution tiers — interpreter, JIT, AOT — literally share one semantics.
- **No bytecode in the binary.** The bootstrap rebuilds each function's constant pool inside its
  shim `ObjFunction` (strings re-interned, classes rebuilt with their method tables, nested
  functions wired by pointer) and sets `fn->nativeCode` to the emitted C function — so
  `OP_CALL`, first-class function values, and method dispatch all reach compiled code through
  the *existing* `call_function` dispatch, and the GC traces the whole constant graph through
  the script function exactly as the interpreter does. The `chunk.code` arrays stay empty.
- **The interpreter stays in the runtime** (it is small): dynamically assembled functions — the
  `Assemble` native the self-hosting track is built on — still execute on it, *inside an AOT
  binary*. `tests/cases/selfhost*.bk` compile and pass as native executables.
- **Runtime packaging.** The emitted `.c` compiles together with the runtime sources
  (`-DBK_NO_JIT`; the `fn->nativeCode` dispatch is now unconditional while only the JIT's
  profile gating sits behind the flag). The build finds them next to the `brokm` binary or via
  `$BROKM_HOME`. Errors before codegen (parse/typecheck) report exactly as `brokm run` does,
  at build time.
- **Verified.** `make test-aot`: all 52 goldens compiled to native executables and diffed
  against the same expected outputs (including the static-type-error cases via build stderr and
  the GC write-barrier cases); plus the standard four-way matrix, embed, selfcompile, bootstrap,
  and fixpoint — 0 warnings. CI runs `test-aot` on macOS arm64 and Linux x86-64 (the emitted C
  is architecture-independent).
- **Speed today:** Fib(34) ~1.6× faster than the interpreter, slower than the JIT — the
  generated C re-reads `vm->stackTop` through the global on every push/pop where the JIT keeps
  it in a register. Closing that gap became v0.14 (done: cached `sp` + direct AOT→AOT calls).
- **Deferred (the kernel path, sketched):** v0.15 adds a `--freestanding` runtime profile —
  no libc natives, GC compiled out (manual `MAlloc`/`Free` + `Peek`/`Poke` already exist), no
  front-end/interpreter in the link, custom entry point — which is what booting a kernel needs.
  Cross-compilation is nearly free already (`--cc` passthrough + arch-independent C).

## Self-hosting (north star)

Can brokm compile itself? Eventually, yes — and brokm now hosts the front-end of a compiler.

- **Step 1 — lexer ✅.** `examples/lexer.bk` is a **tokenizer for brokm written in brokm**:
  it scans a source string with `CharAt`/`Substr`/`Len` and classifies
  identifiers/keywords/numbers/strings/operators.
- **Step 2 — parser + evaluator ✅.** `examples/calc.bk` is a **recursive-descent parser and
  tree-walking evaluator written in brokm**: it tokenizes, parses into an AST with correct
  operator precedence, and evaluates. Building it required a language fix — **class names are now
  valid type annotations** for fields, parameters, and return values, so a self-referential
  `class Node { Node left; Node right; }` and `Node ParseExpr()` parse. Verified red/green by
  `tests/cases/classref.bk` (a self-referential linked list).

- **Already enough:** functions/recursion, arrays, classes/structs (incl. self-referential),
  manual memory, strings + the string/IO stdlib.
- **Map/hash type ✅** (v0.7) — `MapNew`/`MapGet`/`MapSet`/… give the symbol-table primitive.
- **Methods ✅** (v0.8) — instance methods with `this`, so a compiler's data types can carry
  behavior.
- **Modules ✅** (v0.10) — `#include "file"` splits a compiler across files.
- **Parser conveniences ✅** (v0.10.1) — array-of-class declarations (`Node[] xs`) and forward
  declarations (`RetType Name(params);`).
- **Step 3 — code generator ✅.** `examples/complib.bk` is a **compiler written in brokm**: it
  lexes and parses a small statement language (`name = expr;`, `print expr;`, integer expressions
  with variables), allocates variables in a **symbol-table map** (`MapNew`…), **emits stack
  bytecode** (`PUSH/LOAD/STORE/ADD/…/PRINT`), and runs that bytecode on a tiny VM whose operand
  stack and variable store live in **`MAlloc`'d memory** — a grand tour of classes, arrays, maps,
  manual memory, and the string stdlib. `examples/compiler.bk` is the demo;
  `tests/cases/selfhost.bk` (`#include`s the library across directories) is the golden test, and
  it verifies in-language that running the generated bytecode matches a direct tree-walk. It runs
  byte-identically on the interpreter, the JIT, and under GC stress — **no C changes were needed**.
- **Step 4 — real bytecode ✅.** `examples/asmlib.bk` reuses complib.bk's front-end unchanged and
  swaps in a back-end that emits the C VM's **actual opcode format**: brokm-generated code now runs
  on the C VM *directly* instead of a toy in-language VM. Two thin C natives form the bridge —
  `Opcode("ADD")` resolves a mnemonic to its real `OpCode` value (the enum in `chunk.h` is the
  single source of truth), and `Assemble(code, consts)` packs a byte array plus a constant pool
  into a real, callable `ObjFunction`. The brokm program then calls it like any other function and
  it executes through the normal `OP_CALL` path. Variables become real VM globals
  (`OP_DEFINE_GLOBAL`/`OP_GET_GLOBAL`), so no slot bookkeeping is needed. `examples/realgen.bk` is
  the demo; `tests/cases/selfhost2.bk` (`#include`s the library across directories) is the golden
  test, byte-identical on the interpreter, the JIT, and under GC stress.
- **Step 5 — control flow ✅.** `examples/realcc.bk` is a self-contained compiler (lexer + parser +
  code generator, depending only on the `Opcode`/`Assemble` natives) for a small imperative language
  with comparisons, `if/else`, and `while`. It emits real `OP_JUMP_IF_FALSE` / `OP_JUMP` / `OP_LOOP`
  with 16-bit big-endian operands and does **in-language jump backpatching** (`EmitJump` reserves a
  placeholder operand, `PatchJump` fills it once the target is known, `EmitLoop` computes the
  backward offset) — mirroring the C compiler's `emit_jump`/`patch_jump`/`emit_loop` exactly, again
  with **no C changes**. `examples/realccdemo.bk` is the demo; `tests/cases/selfhost3.bk` exercises
  nested `if/else`, `while`, and every comparison operator as a deterministic check of the generated
  jumps, byte-identical on the interpreter, the JIT, and under GC stress.
- **Step 6 — functions ✅.** `examples/realcc.bk` grew **function definitions** with parameters,
  recursion, mutual recursion, and calls. Each `func` body is compiled to its **own** chunk and
  packed by `Assemble(code, consts, arity)` (the only C change — an optional arity argument) into a
  real, callable `ObjFunction`, which is stored as a **constant of the enclosing chunk** and bound
  with `OP_DEFINE_GLOBAL`; calls emit `OP_CALL`. Parameters live in **real local slots** (1..arity;
  slot 0 is the function itself), so recursion works. The code generator saves/restores its emit
  buffers and parameter scope around each nested function. `tests/cases/selfhost4.bk` checks `fib`,
  factorial, two-argument calls, mutual recursion, a loop-with-locals, and nested calls — all
  byte-identical on the interpreter, the JIT, and under GC stress.
- **Step 7 — local variables ✅.** `examples/realcc.bk` gained **function-scoped locals**: a
  non-parameter variable assigned inside a function now lives in a real local slot, not a global.
  The generator pre-scans each body for assigned names (recursing through `if`/`while`), hoists them
  into slots after the parameters, and reserves each with a nil push at entry — so a slot exists
  regardless of which branch runs. This fixes a real defect: a recursive function's temporaries used
  to leak into the global namespace and clobber each other across calls (`fac(5)` returned 16
  instead of 120). `tests/cases/selfhost5.bk` checks cases that are only correct with per-call
  locals — a temporary surviving a second recursive call, and a value read after a recursive call —
  with **no C changes** at all. Top-level variables remain globals, matching brokm.
- **Step 8 — arrays ✅.** `examples/realcc.bk` gained **aggregates**: array literals `[...]`,
  indexing `a[i]` (get and set), and **expression statements**, which — because the call path already
  resolves globals — let a toy program call brokm's native stdlib (`Append`, `Len`, …) directly. It
  emits real `OP_ARRAY` / `OP_INDEX_GET` / `OP_INDEX_SET`, again with **no C changes**. Statement
  parsing was generalized: an expression is parsed, and a following `=` marks it an assignment (a
  variable or an index target), otherwise it is a bare expression statement.
  `tests/cases/selfhost6.bk` builds and mutates arrays, grows one with `Append`, and runs a bubble
  sort (nested loops + index get/set + a swap temporary) — byte-identical on the interpreter, the
  JIT, and under GC stress (arrays are heap objects exercising the write barrier).
- **Step 9 — strings ✅.** `examples/realcc.bk` gained **string literals** (`"..."` with `\n`, `\t`,
  `\"`, `\\` escapes), emitted as real `OP_CONSTANT` string values. They concatenate with `+` (the
  VM's runtime string concatenation), print through the native `Print`, and flow through variables,
  arrays, and function parameters — once more with **no C changes**. `tests/cases/selfhost7.bk`
  prints text and concatenations, indexes an array of strings, and labels a computed `fib(10)` —
  byte-identical on the interpreter, the JIT, and under GC stress.
- **Step 10 — type annotations + maps ✅.** `examples/realcc.bk` now accepts **HolyC-style typed
  declarations** — `I64 x = e;`, `I64 x;` (defaults to 0), `U0[] a = e;`, and typed functions
  `U0 f(I64 n) { ... }` with typed parameters — parsed and then discarded (gradual, exactly as brokm
  treats its own scalar keywords). The untyped forms still work, so the grammar is a superset, moving
  the toy syntax materially closer to real brokm. **Maps come for free**: `MapNew`/`MapSet`/`MapGet`
  are native globals, so string-keyed maps work through the call path with no new code.
  `tests/cases/selfhost8.bk` mixes typed and untyped declarations, a typed recursive `fib`, and a
  string-keyed map — **no C changes**, byte-identical four ways.
- **Step 11 — structs/classes ✅.** `examples/realcc.bk` gained `class Name { I64 x; I64 y; }`
  declarations, construction `Name(a, b)` (through the ordinary `OP_CALL` path — `call_value` builds
  the instance), and field access `obj.field` / `obj.field = v` (real `OP_GET_FIELD` /
  `OP_SET_FIELD`). This is the one place a C hook was still needed: classes are compile-time constants
  in the C VM, so a small **`MakeClass(name, fields)` native** assembles the `ObjClass` (holding GC
  off across `class_new`, whose internal field-array allocation would otherwise free the unrooted
  class). A `NULL` literal was added so self-referential structures work. `tests/cases/selfhost9.bk`
  declares `Point`/`Rect`/`Cons`, mutates fields, passes structs to functions, and sums a
  self-referential linked list by both recursion and iteration — byte-identical four ways (with
  `MakeClass` exercised under GC stress).
- **Step 12 — methods ✅.** `examples/realcc.bk` classes now carry **methods** alongside fields —
  `class Counter { I64 n; func Inc() { this.n = this.n + 1; } }` — invoked as `obj.Inc()`. A method
  compiles to its own chunk (like a function) and is attached to the class with one new C hook, the
  **`AddMethod(klass, name, fn)` native** (`table_set` into `ObjClass.methods` + a write barrier,
  mirroring `map_set`); the C VM stores methods in that table at compile time, so this gives brokm a
  runtime way to do the same. The receiver is local slot 0, named `this` (load-bearing: without that
  mapping `this` resolves to an undefined global — verified red/green). Calls emit `OP_INVOKE`
  (receiver, args, then a name-constant + argc), reusing the C VM's method dispatch (field-first, then
  the class's method table). `tests/cases/selfhost10.bk` mutates fields through `this`, calls sibling
  methods (`this.Inc()`), passes an instance to a method, and relies on reference semantics —
  byte-identical four ways (with `AddMethod` exercised under GC stress).
- **Step 13 — `for` / `do`-`while` loops ✅.** `examples/realcc.bk` rounds out its control flow with
  C-style `for (init; cond; incr) { ... }` (init runs once, increment after each body; loop variables
  declared in the init are collected as function-scoped locals) and `do { ... } while (cond);` (body
  runs at least once). Zero C changes — both reuse the existing jump/loop opcodes, desugaring to the
  same init-then-conditional-loop shape. With the file now past the 800-line guideline, the compiler
  was **split into modules** — `realcc_types.bk` / `realcc_lex.bk` / `realcc_parse.bk` /
  `realcc_gen.bk`, stitched together by `realcc.bk` via `#include` (dogfooding v0.10 modules); every
  test and demo that includes `realcc.bk` picks them up transitively. `tests/cases/selfhost11.bk`
  covers top-level, in-function, and nested `for` loops plus a `do`-`while` — byte-identical four ways.
- **Step 14 — `switch` / `case` / `default` / `break` ✅.** `examples/realcc.bk` gained C-style
  `switch` with fallthrough: the discriminant is evaluated once (into a reusable global), equality
  tests chain to each case body, bodies fall through to the next case until a `break`, and `default`
  may sit anywhere in the chain. This forced one enabling **C change** — the compiler's `make_constant`
  now **deduplicates constants** (strict type+payload identity, so an int and an equal float stay
  distinct). realcc had grown enough top-level functions and globals — e.g. the name `Opcode`
  referenced from 31 `ROP_*` initializers — that the `<script>` chunk blew past the single-byte
  256-constant limit; pooling identical constants brings it well under, and is a general win for any
  large brokm program. The switch code generator is its own function (`GenSwitch`) so its constants
  live in a separate chunk. `tests/cases/selfhost12.bk` covers fallthrough, `break`, `default`
  (including no-match), a switch inside a function with locals, and a nested switch — byte-identical
  four ways. (The `arithmetic.bk` `3.5`-vs-`3` regression from a first, too-loose dedup confirmed the
  strict-identity requirement red/green.)
- **Step 15 — HolyC `"...";` print statements ✅.** `examples/realcc.bk` now matches brokm's own print
  grammar: a statement that begins with a string literal is printed, with optional printf-style
  trailing arguments (`"x = %d\n", x;`), going straight through `OP_PRINT` and the same `bk_format`
  the C VM uses (so `%d`/`%s`/`%f`/… all work). Zero C changes — the bridge already exposed everything.
  The toy `print expr;` keyword stays for the existing tests, so the grammar is a superset.
  `tests/cases/selfhost13.bk` covers `%d`, `%s`, multiple arguments, a no-argument string, and a print
  driven by a loop over a recursive `fib` — byte-identical four ways.
- **Step 16 — compiling real brokm source ✅.** The in-brokm compiler now reads an **ordinary brokm
  program from disk** and compiles it, producing output identical to the C compiler. `examples/sample.bk`
  is a real brokm program (the C compiler runs it directly); `examples/selfcompile.bk` reads it with
  `ReadFile` and compiles it with realcc; `make test-selfcompile` runs both and diffs them
  (`selfcompile: OK (C compiler == in-brokm compiler)`). The one gap this surfaced was **comments**:
  real source has `//` and `/* */`, which realcc's lexer now skips (matching brokm's), finally retiring
  the long-standing "no comments inside the compiled program" limitation. To stay in the grammars'
  intersection `sample.bk` uses typed declarations throughout and `U0` for instance variables (realcc
  does not yet accept user class names as type prefixes). `tests/cases/selfhost14.bk` compiles
  `sample.bk` under the four-way (interpreter / JIT / GC-stress) matrix.
- **Step 17 — class names as type prefixes ✅.** realcc now recognizes a declared **class name as a
  type**, exactly as brokm does: it keeps a registry of class names (filled when a class declaration is
  parsed, before its body, so self-references work) and accepts them wherever a builtin type keyword is
  accepted — variable declarations (`Counter c = Counter(0);`), function parameters (`Point a`), return
  types, and self-referential fields (`class Node { I64 val; Node next; }`). A leading `ClassName (` is
  still a constructor call, not a declaration, so the two never collide. Parser-only and gradual (the
  type is discarded), so no codegen change. `examples/sample.bk` now uses natural class-typed
  declarations throughout (no more `U0` stand-ins), and still compiles byte-identically via both the C
  and in-brokm compilers (`make test-selfcompile`). Verified red/green: stubbing the registry makes
  `sample.bk` fail to parse.
- **Step 18 — bitwise, shift, logical, and unary operators ✅.** realcc's expression grammar gained the
  full operator set with **brokm's exact precedence ladder** (`||` < `&&` < `|` < `^` < `&` < equality <
  comparison < shift < additive < multiplicative < unary): bitwise `& | ^`, shifts `<< >>`, logical
  `&& ||` (short-circuit, via `OP_JUMP_IF_FALSE`/`OP_JUMP_IF_TRUE`), and unary `!` and `~`. Equality and
  comparison, previously one level, are now split to match brokm. The bitwise/shift operators map to the
  existing `OP_BIT_*`/`OP_SHL`/`OP_SHR` opcodes; no C change. Confirmed against brokm directly — realcc
  and the C compiler produce identical results for every operator (including the value semantics of
  `&&`/`||`). `examples/sample.bk` gained a bit-twiddling section (`PopCount`, `IsEven`, flag masks);
  `tests/cases/selfhost15.bk` is a focused operator + precedence test.
- **Step 19 — `else if` chains and function prototypes ✅.** Two constructs realcc's own source leans on
  heavily (34 `else if` and several forward declarations) but the toy grammar lacked. An `else` may now
  be followed by either a block or another `if` (the else body becomes a single nested if-statement), so
  `if/else if/.../else` ladders work. And a function may be a **prototype** — `Type Name(params);` with
  no body — which parses with `thenB == NULL` and emits nothing; the real definition later binds the
  global, exactly as brokm treats forward declarations. Both confirmed identical to the C compiler.
  `examples/sample.bk` gained a `Grade`/`Sign` example (forward-declared `Grade`, `if/else if/else`
  ladder); `tests/cases/selfhost16.bk` is a focused test.
- **Step 20 — braceless bodies + deeper recursion ✅.** Working toward realcc compiling its own modules
  surfaced two blockers (found by feeding each `realcc_*.bk` module back through realcc). First, the C
  VM's call-frame limit (`BK_FRAMES_MAX`) was only 64 — realcc's layered recursive-descent parser, ~12
  precedence levels deep, overflowed it on realistic nested-call source; raised to 256 (still iterative
  in the interpreter, fine under JIT and GC stress). Second, realcc required braces on every control-flow
  body, but brokm (and realcc's own source — `if (word == "print") Append(...);`, `while (c) i = i + 1;`)
  allows a single statement; a new `ParseBody` accepts either a block or one statement for `if`/`else`/
  `while`/`for`/`do` (function bodies still require braces, as in brokm). With both, realcc now compiles
  its `realcc_types` and `realcc_lex` modules (up from `realcc_types` only). `tests/cases/selfhost17.bk`
  covers braceless `if`/`else if`/`else`/`for`/`while`/`do`; `examples/sample.bk` gained a braceless
  `Clamp`.
- **Step 21 — local/global scoping ✅.** realcc's step-7 codegen hoisted *every* name assigned in a
  function as a local, so a function writing a top-level global silently created a shadowing local
  instead — the first real blocker to realcc compiling its own source (`Parse` sets the shared
  parser-state globals `toks`/`pos`, which under the old rule vanished into `Parse`-locals, leaving the
  readers an empty token array). Fixed to match brokm's actual scoping: a local is introduced **only** by
  a typed declaration (`I64 x = e`) or a parameter; a bare `x = e` targets an existing local or global and
  never creates one. Typed declarations now emit a distinct statement kind, and codegen pre-collects the
  top-level globals so a bare assignment to a global compiles to `OP_DEFINE_GLOBAL` while an untyped temp
  still hoists (recursion through untyped locals keeps working). Pure `.bk` change.
  `tests/cases/selfhost18.bk` covers a function writing a global, recursion through a bare-assign local,
  and a typed global updated in a function — byte-identical four ways, red/green verified.
- **Step 22 — true bootstrap ✅.** realcc now compiles its **own complete source**, and the self-compiled
  compiler produces byte-identical output to the C compiler: C compiler == one-stage realcc ==
  realcc-compiled-realcc, all three agree on `sample.bk`. The last blocker was realcc's `AddConst` using a
  one-byte constant operand with no deduplication — `ParseStmt` alone (a long if/else chain over token
  kinds) emitted more than 256 distinct constants, wrapping the byte and reading the wrong constant — the
  same >256-constant overflow the C compiler solved in step 14. Fixed (pure `.bk`) by reusing an identical
  existing constant before appending; safe because realcc's constants are only ints, interned strings, and
  object values (no floats to confuse with equal-valued ints), and dedup changes indices but not output, so
  all golden tests stay byte-identical. `examples/bootstrap.bk` is the two-stage driver (realcc compiles
  realcc's four concatenated modules, runs the result to rebind `Compile`/`Parse`/`Tokenize`/… as the
  brokm-compiled functions, then self-compiles `sample.bk`); `make test-bootstrap` diffs it against the C
  compiler, and `tests/cases/selfhost19.bk` is the deterministic bootstrap under the four-way matrix.
- **Path:** lexer ✅ → parser/AST ✅ → code generator ✅ → real bytecode ✅ → control flow ✅ →
  functions ✅ → locals ✅ → arrays ✅ → strings ✅ → types + maps ✅ → structs ✅ → methods ✅ →
  `for`/`do`-`while` ✅ → `switch` ✅ → HolyC print ✅ → real brokm source ✅ → class-typed declarations ✅
  → full operator set ✅ → `else if` + prototypes ✅ → braceless bodies ✅ → local/global scoping ✅ →
  **self-hosting bootstrap ✅**. realcc now compiles its own complete source, byte-identical to the C
  compiler (`make test-bootstrap`). Still ahead — much later — a runtime in brokm, retiring the C
  bootstrap entirely. The baseline JIT matters here: a self-hosted compiler is compute-heavy.

## v0.13 — Scripting stdlib + library distribution ✅ (done)

brokm became a practical scripting language. Three pieces:

- **Scripting natives** (`natives.c`): `Args()` (CLI arguments, plumbed through `main.c` and
  the AOT-emitted `main`), `Env`, `Exit`, `Shell` (exit status), `ShellStr` (captured stdout),
  `Input` (stdin line), `Time`/`TimeMs`, `Sleep`, `AppendFile`, `FileExists`.
- **A standard library written in brokm** under `lib/std/` — `str.bk`, `arr.bk`, `io.bk`,
  `path.bk`, `os.bk`, plus the `std.bk` umbrella. One flat namespace, so public names are
  module-prefixed (`StrSplit`, `ArrSort`, `PathJoin`, …). `#include "std/str.bk"` works from
  any script: the lexer resolves includes relative to the including file, then
  `$BROKM_HOME/lib`, then `~/.brokm/lib`.
- **Install/update in one place**: `install.sh` makes `~/.brokm` a clone of the repo (binary,
  `lib/`, and the `src/` tree `brokm build` links against, together); re-running it updates.

Also: the Makefile now emits header dependencies (`-MMD -MP`), so editing a `.h` rebuilds
exactly its dependents — the historical "stale object after a header edit" hazard is gone —
and `docs/CODEMAP.md` orients both humans and AI tools in the codebase.

## v0.14 — Optimize the emitted C ✅ (done)

Goal was to meet or beat the JIT on `make bench`; achieved with two of the four planned
optimizations, so the milestone closed there:

- **Cached stack-top**: each `bk_f<i>` keeps `vm->stackTop` in a C local (`sp`), with
  `FLUSH()`/`RELOAD()` bracketing every `jit_h_*` helper call — the same discipline the
  arm64 JIT applies with its dedicated register. Pure stack traffic stays in registers.
- **Direct AOT→AOT calls**: `OP_CALL` checks at runtime that the callee is an AOT-compiled
  function of matching arity (and that the stack has headroom) and invokes its `bk_f`
  directly, skipping `jit_h_call` → `call_value` dispatch. The check is dynamic because
  globals are mutable — there is no statically-known callee in brokm.
- `make bench` grew an AOT tier. Fib(34) on an M4: interpreter ~1.4s, JIT 0.15s,
  AOT 0.24s → **0.12s**.

Dropped (recorded so nobody re-attempts them naively):

- **Locals as C variables** — locals *are* VM stack slots, i.e. GC roots. Hoisting them into
  C locals hides them from the collector unless every safepoint spills them back, which
  costs what it saves. Would only make sense bundled with unboxing + a side root map.
- **Unboxed `I64`** — gradual typing means a static `I64` slot can hold a float at runtime;
  unboxing needs guards + a deopt story per function, a much bigger project than the
  remaining ~20% win. Revisit if `--freestanding` (v0.15) makes AOT speed critical.

## v1.0.0 — first stable release ✅ (2026-06)

The first tagged, stable release. It bundles everything above — bytecode VM, generational
GC, gradual static type checker, typed bytecode, baseline JIT (arm64 + x86-64), AOT
compile-to-C (now meeting the JIT, linking the runtime core only), the self-hosting
bootstrap, multi-instance embedding API, scripting stdlib + `lib/std/`, `install.sh`,
editor support, and the macOS/Linux CI matrix — under the first stability promise:
golden-test outputs are byte-identical across all four execution paths
(interpreter / forced-JIT / AOT / GC-stress) on both platforms, and every release is cut
only from a fully green matrix. Two v0.15 increments (front-end-free AOT link, `--cflags`)
shipped early and are included.

## What's next

With the bootstrap, multi-instance VMs, portability/CI, the AOT compile-to-C backend, the
scripting stdlib, and AOT performance parity done, the road points toward the long-term
vision: a `--freestanding` profile good enough to boot a kernel written in brokm. In order:

### v0.15 — `--freestanding` runtime profile (the kernel path)

- A `BK_FREESTANDING` build of the runtime for `brokm build`: no libc natives (subset table),
  GC compiled out (manual `MAlloc`/`Free` + `Peek`/`Poke` already exist), no
  parser/typecheck/compiler/interpreter in the link, custom entry point, `--target`/linker
  passthrough. The v0.12 architecture was shaped so generated code depends only on the helper
  ABI + object constructors — both separable from the front end.
- **Landed so far:** AOT binaries link the runtime core only (`-DBK_NO_FRONTEND` compiles
  `interpret()` out of `vm.c`; 12 runtime TUs instead of 19, ~13% smaller binaries — the
  interpreter loop itself stays because `Assemble`d functions run on it),
  `--cflags <flags>` passthrough (with `--cc`, covers `--target`/`-static`/`-L`/`-l`), and
  the **output seam** (`output.c`): every byte a program prints now flows through one
  `bk_putchar` — the single function a freestanding host replaces with serial/VGA/syscall
  output — with `bk_sink_*` formatting values to a `BkSink` (`bk_stdout`/`bk_stderr`).
  Value/object printing and the `Print`/`OP_PRINT` formatter were unified onto it, and
  freestanding builds now use libc-free integer/string/char formatting plus a small
  float display path for common `%g`-style values. Byte-identical across all five paths.
  `brokm build --freestanding` now defines `BK_FREESTANDING`, drops `-lm`, compiles out
  hosted-only natives (file IO, libm math, process/env/stdin/time), and rejects those calls
  at AOT typecheck time.
- **Still open:** `gc.c` compiled to a bump-allocator stub with collection off,
  full libc-free printf flags/precision plus the remaining diagnostic/introspection string
  builders, and a `-nostdlib` entry point (`_start`-style, no `main`). Open design question:
  string/array/map objects allocate — decide whether freestanding keeps the object heap
  (bump, never freed) or restricts programs to scalars + `MAlloc`/`Peek`/`Poke` in a first cut.

### Hardening + deferred fixes

- **Postfix `++`/`--` value semantics** and the **switch-scope** simplification, deferred since
  v0.4. (The three-stage fixpoint and the `Assemble` constant guard landed in v0.11.2.)

### Later (unscheduled)

- **A runtime in brokm** — the long-term self-hosting star: retire the C bootstrap entirely.
- **Formal language spec** — SYNTAX.md is a tour, not a contract.
- **JIT extensions** — backtrace lines for JIT'd frames; inlining bitwise/unary/aggregate ops;
  keeping the cached stack-top in a register across calls.
- **Language sugar** — map literals `{...}` + any-value keys; inheritance / static / bound
  methods; namespaced imports (`math.Sqrt`); multiple declarators per statement.

## Language features tracked across milestones

- Multiple declarators per statement; command-style function calls; HolyC sub-switches.
- Methods on classes ✅ (v0.8); inheritance, static methods, and first-class/bound methods still
  open. `&`/`*` on managed values (raw `MAlloc` only).
- Modules / multi-file programs ✅ (v0.10, `#include`); namespaced imports still open.
