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

## v0.3 — Generational GC ✅ (done) · manual-memory mode (deferred)

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

## v0.3.5 — Aggregates: arrays, structs, pointers (prerequisite unlock)

Add fixed/dynamic arrays and `class`/`struct` with mutable fields, plus a pointer/reference
notion. This unlocks two deferred pieces at once: the GC write barrier gets real call sites
(old→young mutation), and **manual memory** (`MAlloc`/`Free`, regions) gets something to manage.

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
