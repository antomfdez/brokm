# brokm — next steps (updated 2026-06-10)

Status: self-hosting bootstrap DONE; v0.11.x (multi-instance, portability+CI, hardening) DONE;
**v0.12 AOT compile-to-C DONE** (2026-06-10): `brokm build file.bk -o app` emits C from
bytecode (src/cemit.c, one `JitFn`-shaped C function per chunk on the shared `jit_h_*` helper
ABI; constants rebuilt in shim ObjFunctions, no bytecode in the binary) and drives the system
cc against the runtime sources with -DBK_NO_JIT (src/aot.c; `$BROKM_HOME` or argv[0]-relative).
CLI gained `run`/`build` subcommands with full back-compat. `make test-aot` = all 52 goldens
compiled native; CI runs it on both OSes. Long-term vision: a `--freestanding` profile good
enough to write a kernel in brokm.

## Priority order

### 1. v0.13 — optimize the emitted C (next up)

- Locals as C variables; unboxed I64 fast paths the typechecker proves; direct `bk_f<i>` calls
  for statically-known callees; cached stack-top flushed around helper calls (the JIT's trick).
  Goal: meet or beat the JIT on `make bench` (today AOT Fib(34) ≈ 1.6× interpreter, JIT ≈ 2.5×).

### 2. v0.14 — `--freestanding` runtime profile (the kernel path)

- BK_FREESTANDING runtime subset: no libc natives, GC compiled out (manual memory only), no
  front-end/interpreter in the link, custom entry, `--target`/linker passthrough.

### 3. Remaining deferred fixes

- Postfix `++`/`--` value semantics + switch-scope fix (deferred since v0.4).

### 4. Later (unscheduled)

Runtime in brokm (retire the C bootstrap); formal language spec; JIT extensions (backtrace
frames, inline bitwise/unary/aggregates, stack-top in register across calls); thread-per-VM
(thread-local current pointer + per-VM/locked JIT pool); map literals + any-value keys;
inheritance / static / bound methods; namespaced imports; multiple declarators.

## 이전 계획

v0.12 AOT compile-to-C + CLI subcommands — completed 2026-06-10 (docs/ROADMAP.md v0.12).
v0.11.2 hardening (fixpoint + Assemble guard) — completed 2026-06-10 (docs/ROADMAP.md v0.11.2).
v0.11.1 portability + CI — completed 2026-06-09 (see docs/ROADMAP.md v0.11.1 section).
v0.11 multi-instance VMs — completed 2026-06-09 (see docs/ROADMAP.md v0.11 section).
