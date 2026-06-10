# brokm — next steps (updated 2026-06-10)

Status: self-hosting bootstrap DONE; v0.11 multi-instance VMs DONE; v0.11.1 portability + CI
DONE; **v0.11.2 hardening DONE** (2026-06-10): three-stage bootstrap fixpoint (`make
test-fixpoint` + selfhost20 in the matrix; ChunkCode/ChunkConsts/ValueDesc introspection
natives + in-brokm recursive bytecode diff in examples/fixpoint.bk) and the `Assemble`
>256-unique-constants / out-of-range-byte guard (stderr + NULL, tests/cases/asm_guard.bk).

## Priority order

### 1. Remaining deferred fixes (next up)

- Postfix `++`/`--` value semantics + switch-scope fix (deferred since v0.4).

### 2. Later (unscheduled)

Runtime in brokm (retire the C bootstrap); formal language spec; JIT extensions (backtrace
frames, inline bitwise/unary/aggregates, stack-top in register across calls); thread-per-VM
(thread-local current pointer + per-VM/locked JIT pool); map literals + any-value keys;
inheritance / static / bound methods; namespaced imports; multiple declarators.

## 이전 계획

v0.11.2 hardening (fixpoint + Assemble guard) — completed 2026-06-10 (docs/ROADMAP.md v0.11.2).
v0.11.1 portability + CI — completed 2026-06-09 (see docs/ROADMAP.md v0.11.1 section).
v0.11 multi-instance VMs — completed 2026-06-09 (see docs/ROADMAP.md v0.11 section).
Doc edits for the post-bootstrap roadmap — completed 2026-06-09.
