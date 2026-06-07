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
| `main.c` | CLI / REPL |

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

## Embedding model

brokm uses a single process-global `VM` (`extern VM vm;`), which keeps `object.c` and `natives.c`
free of VM-pointer plumbing. The public `brokm.h` API (v0.9) lets a host exchange scalar and
string values, register C natives, read/set globals, and call brokm functions from C, on top of
`init / eval / run_file / shutdown`. **Multi-instance embedding** (replacing the global `VM`)
remains a later milestone.
