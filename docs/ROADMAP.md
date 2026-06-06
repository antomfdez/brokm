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

## v0.5 — Baseline JIT ✅ (done, arm64 + x86-64 / macOS)

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

**Deferred (v0.5.x):** a **Linux** x64 path (plain `PROT_EXEC` mmap, no `MAP_JIT`); JIT'd frames
in the error backtrace (native frames currently omit their backtrace line); inlining bitwise/unary
ops and aggregates; keeping the cached stack-top in a register across calls.

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

**Deferred to v0.6.x / v0.7:** a richer embedding API (register natives + exchange values from C),
**multi-instance VMs** (replacing the global `VM vm` — a cross-cutting refactor), and a formal
language spec.

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
- **Still needed:** modules / multi-file programs, methods (today: free functions over instances),
  and smaller parser conveniences (array-of-class declarations `Node[] xs`, forward declarations).
- **Path:** lexer ✅ → parser/AST ✅ → a code generator emitting the existing bytecode (runnable
  on the current C VM), then — much later — a runtime in brokm, retiring the C bootstrap. The
  baseline JIT matters here: a self-hosted compiler is compute-heavy.

## Language features tracked across milestones

- Multiple declarators per statement; command-style function calls; HolyC sub-switches.
- Methods on classes; `&`/`*` on managed values (currently free functions + raw `MAlloc` only).
- Modules / multi-file programs.
