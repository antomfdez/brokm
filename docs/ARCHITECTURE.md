# brokm architecture

## Pipeline

```
source (.bk)
   │  lexer.c        scanner → Token stream
   ▼
 tokens
   │  parser.c       recursive descent (statements) + Pratt (expressions)
   ▼
 AST  (ast.c/.h)     the seam for the type checker (v0.4) and the JIT (v0.5)
   │  typecheck.c    static type-checking pass over the AST (gradual; v0.4)
   ▼
 AST  (typed)
   │  compiler.c     single-pass AST walk → bytecode
   ▼
 ObjFunction + Chunk (chunk.c)   bytecode, constant pool, line table
   │  vm.c           stack-based bytecode interpreter
   ▼
 output
```

Parsing and code generation are deliberately **separate passes** with the AST between them.
A combined single-pass compiler would be smaller, but the AST is where static typing and a JIT
attach cleanly later — so it earns its keep.

## Modules

| File | Responsibility |
|------|----------------|
| `common.h` | fixed-width int aliases (`U8`…`I64`,`F64`), VM size tunables |
| `memory.c` | `reallocate()` — the **single allocation chokepoint**; triggers the GC |
| `gc.c` | generational mark-sweep collector (minor/major, write barrier, remembered set) |
| `value.c` | `Value` tagged union, equality, truthiness, printing, constant arrays |
| `object.c` | heap objects (`ObjString/Function/Native/Array/Class/Instance/Map`); interning; teardown |
| `table.c` | open-addressing hash table (globals, interned strings, instance/map fields) |
| `lexer.c` | hand-written scanner; resolves `#include` via a source-frame stack |
| `ast.c` | AST nodes, list helpers, recursive free |
| `parser.c` | tokens → AST |
| `types.c` | the static type lattice (`Type`) and assignability rules |
| `typecheck.c` | gradual static type-checking pass over the AST (v0.4) |
| `compiler.c` | AST → bytecode |
| `chunk.c` | bytecode buffer |
| `vm.c` | the interpreter loop, call frames, globals |
| `jit.c` | JIT driver: executable-page pool, profiling, dispatch + Value-layout asserts (v0.5) |
| `jit_arm64.c` / `jit_x64.c` | per-arch encoders + bytecode walker (arm64 / x86-64) |
| `debug.c` | bytecode disassembler |
| `natives.c` | the native stdlib — printf formatter, I/O, strings, math, maps, manual memory, JIT bridge |
| `api.c` | public `brokm.h` implementation (value exchange, host natives, calls; v0.9) |
| `cemit.c` | AOT back-end: bytecode chunks → C source (v0.12) |
| `aot.c` | `brokm build` driver: front-end, emission, cc invocation, runtime discovery (v0.12) |
| `main.c` | CLI (`run` / `build` subcommands) / REPL |

## Value representation

`Value` is a tagged union of `Nil | Bool | Int(I64) | Float(F64) | Ptr(void*) | Obj*`
(`Ptr` is a raw, GC-invisible pointer from `MAlloc`). All access goes through the
`IS_*` / `AS_*` / `*_VAL` macros in `value.h`, so the representation can later switch to
NaN-boxing for speed without touching any call site.

## Heap objects and the GC seam

Every heap object begins with an `Obj` header:

```c
struct Obj {
  ObjType type;
  U8 mark;          // GC mark bit          (used in v0.2)
  U8 gen;           // generation index     (used in v0.3)
  struct Obj *next; // intrusive list of all objects
};
```

The header and the intrusive `next` list exist **now**, before any collector does, so that:
- v0.1 frees everything at exit by walking the list (no leaks at exit), and
- v0.2 mark-sweep and v0.3 generational GC drop in without changing object layouts.

All allocation funnels through `reallocate()` in `memory.c`; that is the one place a future
collection is triggered.

## Bytecode VM

A stack machine. Each `CallFrame` records its `ObjFunction`, an instruction pointer, and a
`slots` window into the shared value stack. Globals are late-bound by name through the hash
table; locals are slot indices into the current frame. Strings are interned, so string equality
and variable-name resolution are pointer comparisons.

Dispatch is a portable `switch`. A computed-goto fast path can be added behind a macro later.

## Three execution tiers, one semantics

A function body can execute three ways, and all three run on the **same VM value stack**
through the **same runtime helpers** (`jit_h_*`, defined in `vm.c`):

1. **Interpreter** (`run_until`) — the reference. Its opcode cases for globals, equality,
   unary ops, arrays, indexing, fields, and method dispatch *are* calls to the `jit_h_*`
   helpers, so the tiers cannot drift.
2. **JIT** (v0.5) — hot functions compile to machine code: int arithmetic/branches inlined
   with tag-guard deopts, everything else a call into the same helpers.
3. **AOT** (v0.12, optimized v0.14) — `brokm build` emits one C function per chunk
   (`bool bk_f<i>(Value *slots)`, the `JitFn` signature): the interpreter specialized to that
   chunk, labels at jump targets, guarded int fast paths for the typed opcodes, helpers for the
   rest. Each emitted function caches `vm->stackTop` in a C local (`sp`), flushed/reloaded
   around helper calls — the same discipline the JIT applies with a register — and `OP_CALL`
   invokes an AOT-compiled callee's `bk_f` directly when arity matches. The bootstrap rebuilds
   the constant graph inside shim `ObjFunction`s (pools populated, `chunk.code` empty — no
   bytecode ships) and sets `fn->nativeCode` to the emitted function, so the existing
   `call_function` dispatch routes `OP_CALL`, first-class function values, and `OP_INVOKE` to
   compiled code, and the GC roots everything through the script function as usual. The
   generated `.c` is architecture-independent and compiles with the **runtime core only**
   under `-DBK_NO_JIT -DBK_NO_FRONTEND` (v0.15: no lexer/parser/typechecker/compiler in the
   link); the interpreter loop stays in the binary so dynamically assembled functions
   (`Assemble`) still run.

## Embedding model

The runtime is **instance-based** (v0.11): `brokm_new()` creates an independent `BrokmVM` — its
own value stack, globals, interned strings, object list, and collector — and a process may hold
any number of them, created, interleaved, and destroyed in any order.

Internally the interpreter, GC, allocator, and natives act on a **current-instance pointer**
(`extern VM *vm;`) rather than threading a parameter through every function — which keeps
`object.c` and `natives.c` free of VM-pointer plumbing. Every public API entry point makes its
target the current instance and restores the previous one on exit, so a host native running on
one VM can drive another VM and return cleanly. Two pieces of state stay process-wide: the JIT's
executable-page pool (reference-counted by live VMs, since each VM's generated code lives in it;
generated code bakes its *owning* VM's addresses, which is sound because functions never migrate
between instances) and the compile-phase scratch state (parser/checker), which is reset per
compile. Both are why the runtime is single-threaded: VMs interleave freely on one thread, but
one VM per *thread* would need a thread-local current pointer and a per-VM (or locked) page pool
— future work.

The public `brokm.h` API lets a host exchange scalar and string values, register C natives (per
VM — inside one, `brokm_current()` is the calling instance), read/set globals, and call brokm
functions from C.
