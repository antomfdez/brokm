# brokm architecture (v0.1)

## Pipeline

```
source (.bk)
   │  lexer.c        scanner → Token stream
   ▼
 tokens
   │  parser.c       recursive descent (statements) + Pratt (expressions)
   ▼
 AST  (ast.c/.h)     the seam for the future type checker (v0.4) and JIT (v0.5)
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
| `memory.c` | `reallocate()` — the **single allocation chokepoint**; the GC will trigger here |
| `value.c` | `Value` tagged union, equality, truthiness, printing, constant arrays |
| `object.c` | heap objects (`ObjString`, `ObjFunction`, `ObjNative`); string interning; teardown |
| `table.c` | open-addressing hash table (globals + interned strings) |
| `lexer.c` | hand-written scanner |
| `ast.c` | AST nodes, list helpers, recursive free |
| `parser.c` | tokens → AST |
| `compiler.c` | AST → bytecode |
| `chunk.c` | bytecode buffer |
| `vm.c` | the interpreter loop, call frames, globals |
| `debug.c` | bytecode disassembler |
| `natives.c` | `Print` and the shared printf-style formatter |
| `api.c` | public `brokm.h` implementation |
| `main.c` | CLI / REPL |

## Value representation

`Value` is a tagged union of `Nil | Bool | Int(I64) | Float(F64) | Obj*`. All access goes
through the `IS_*` / `AS_*` / `*_VAL` macros in `value.h`, so the representation can later switch
to NaN-boxing for speed without touching any call site.

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

v0.1 uses a single process-global `VM` (`extern VM vm;`), which keeps `object.c` and `natives.c`
free of VM-pointer plumbing. The public `brokm.h` API is intentionally narrow
(`init / eval / run_file / shutdown`). Multi-instance embedding and a value-exchange C API are a
later milestone.
