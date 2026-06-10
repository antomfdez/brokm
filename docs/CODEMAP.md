# Code map

One file = one subsystem. The pipeline, left to right:

```
source.bk ──lexer──► tokens ──parser──► AST ──typecheck──► AST
                                                  │
                                              compiler
                                                  │
                                               bytecode ──┬─► vm.c run loop (interpreter)
                                                          ├─► jit.c (+jit_arm64/jit_x64) native code
                                                          └─► aot.c + cemit.c emitted C → executable
```

## src/ — the runtime

| File | Lines¹ | Owns |
|---|---|---|
| `main.c` | ~150 | CLI: REPL / `run` / `build` / legacy `brokm f.bk [args]`; stashes argv for `Args()` |
| `lexer.c` | ~460 | Tokens; `#include` resolution (relative → `$BROKM_HOME/lib` → `~/.brokm/lib`), include-once via realpath |
| `parser.c` | ~880 | Recursive-descent → AST; class-name registry so `Point p` parses; 1-token lookahead |
| `ast.c/.h` | ~210 | Expr/Stmt node structs + free; carries `Type` annotations from the parser |
| `typecheck.c` | ~740 | Static pass between parse and compile; gradual (TY_UNKNOWN = top); native NAMES[] registry |
| `types.c/.h` | small | `Type` POD + assignability (strict only for class identity) |
| `compiler.c` | ~690 | AST → bytecode; classes/methods built at compile time as constants; constant dedup |
| `chunk.c/.h` | small | Bytecode container + the OpCode enum (single source of truth, see `Opcode()` native) |
| `vm.c` | ~810 | The run loop (`run_until`), call/invoke dispatch, the `jit_h_*` semantic helpers all tiers share |
| `value.c/.h` | small | 16-byte tagged `Value` (layout baked into JIT/AOT — asserted at startup) |
| `object.c/.h` | ~215 | Heap objects: string/function/native/array/class/instance/map |
| `table.c/.h` | small | String-keyed hash table (interning, globals, methods, maps); `count` includes tombstones |
| `memory.c` | small | `reallocate()` chokepoint — every GC trigger flows through here |
| `gc.c` | ~230 | Generational mark-sweep; remembered set; `gc_write_barrier` (load-bearing, see CLAUDE.md) |
| `natives.c` | ~900 | Stdlib primitives: print/format, maps, manual memory, strings, I/O, math, scripting/OS, self-hosting bridge |
| `jit.c` | shared | JIT driver: page pool, thresholds, per-arch hooks, Value-layout self-test |
| `jit_arm64.c` / `jit_x64.c` | ~430 ea | Per-arch encoders + chunk walker; faithful stack mirror of the interpreter |
| `aot.c` | ~260 | `brokm build`: find runtime sources (`$BROKM_HOME` or argv0 dir), drive cc |
| `cemit.c` | ~520 | Bytecode → C emitter (each chunk = a C function on the `jit_h_*` ABI) + `bk_bootstrap()` constant-graph rebuild |
| `api.c` + `include/brokm.h` | ~220 | Public embedding API: multi-instance VMs, value handles, host natives, `BROKM_VERSION` |
| `debug.c` | small | Disassembler |
| `common.h` | small | Limits (BK_FRAMES_MAX 256 etc.), build knobs (BK_NO_JIT, BK_DEBUG_STRESS_GC, …) |

¹ approximate, kept under ~800 by convention.

## lib/ — the standard library written in brokm

Flat namespace ⇒ module prefixes. `#include "std/std.bk"` pulls everything.

| Module | Provides |
|---|---|
| `std/str.bk` | StrSplit/Join/Trim*/Replace/Contains/StartsWith/EndsWith/Repeat/Pad*/Upper/Lower/Cmp/IndexOfFrom/IsSpace |
| `std/arr.bk` | ArrIndexOf/Contains/Sum/Min/Max/Slice/Copy/Reverse/Sort (numeric)/SortStr |
| `std/io.bk` | ReadLines/WriteLines/AppendLine |
| `std/path.bk` | PathJoin/Dir/Base/Ext |
| `std/os.bk` | EnvOr/ShellLines/ShellOk |

C-native scripting primitives live in `natives.c`: `Args Env Exit Shell
ShellStr Input Time TimeMs Sleep AppendFile FileExists ReadFile WriteFile`.

## Recipes

- **New native**: function in `natives.c` (GC-root fresh objects — copy
  `native_args`), register in `natives_register()`, add to `NAMES[]` in
  `typecheck.c`, golden test.
- **New stdlib function**: the right `lib/std/*.bk`, module-prefixed name,
  extend that module's golden test (`tests/cases/std_*.bk`, regenerate
  `.expected` from verified output).
- **New opcode**: enum in `chunk.h`, emit in `compiler.c`, semantics as a
  `jit_h_*` helper in `vm.c` called from the run-loop case, JIT walker either
  inlines-with-guard or bails, `cemit.c` emits the helper call (bracketed
  `FLUSH(); ...; RELOAD();` — emitted code caches `vm->stackTop` in `sp`),
  disasm in `debug.c`, `OPCODE_TABLE` in `natives.c` if brokm code should
  assemble it.
- **New syntax**: token in `lexer.c`/`token.h`, parse in `parser.c`, AST node
  in `ast.c/.h` (+free), check in `typecheck.c`, compile in `compiler.c`;
  consider whether the self-hosting compiler (`examples/realcc_*.bk`) needs it.

## examples/ — self-hosting track

`realcc.bk` (4 modules) is a brokm compiler written in brokm that emits this
VM's real bytecode via the `Opcode`/`Assemble` natives; `bootstrap.bk` and
`fixpoint.bk` prove it compiles itself to a fixpoint. Tests `selfhost*.bk`
pin all of it. Touching the bytecode format means revisiting these.
