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
- **v0.4.1** — emit typed bytecode (specialized int/float arithmetic opcodes) using the
  resolved `Expr` types the checker now records. Kept separate so the working VM hot path is
  untouched by the checker itself.
- **Separate fix** — the v0.1 postfix `++`/`--` value semantics and switch-scope
  simplification (independent of typing).

## v0.5 — Baseline JIT

Compile hot functions to native code into `mmap`'d executable pages (arm64 first, then x64),
with a VM fallback. Profile-gated; benchmark-driven.

## v0.6 — Embedding, stdlib, polish

Richer `brokm.h` (register natives, exchange values, multi-instance VMs), a small standard
library (math, strings, I/O), and a stable language spec.

## Language features tracked across milestones

- Multiple declarators per statement; command-style function calls; HolyC sub-switches.
- Methods on classes; `&`/`*` on managed values (currently free functions + raw `MAlloc` only).
- Modules / multi-file programs.
