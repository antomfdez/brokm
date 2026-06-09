/* vm.h - the stack-based virtual machine. */
#ifndef BROKM_VM_H
#define BROKM_VM_H

#include "object.h"
#include "table.h"
#include "value.h"

typedef struct {
  ObjFunction *function;
  U8 *ip;       /* next instruction within function->chunk */
  Value *slots; /* this call's window into the value stack */
} CallFrame;

typedef struct {
  CallFrame frames[BK_FRAMES_MAX];
  int frameCount;
  Value stack[BK_STACK_MAX];
  Value *stackTop;
  Table globals;
  Table strings; /* interned string set (weak references) */
  Obj *objects;  /* intrusive list of every heap object */

  /* Garbage collector state (v0.2 + generational in v0.3). */
  bool gcEnabled;         /* off while the compiler builds unrooted objects */
  size_t bytesAllocated;  /* live bytes tracked through reallocate() */
  size_t nextGC;          /* major collection threshold */
  size_t bytesSinceMinor; /* growth since the last minor collection */
  size_t minorThreshold;  /* minor collection threshold */
  int grayCount;
  int grayCapacity;
  Obj **grayStack; /* worklist of marked-but-not-traced objects */
  int rememberedCount;
  int rememberedCapacity;
  Obj **rememberedSet; /* old objects pointing into the young generation */

  /* C-API temporary roots: host-created values (e.g. strings from the embedding
   * API) kept alive between construction and use. Cleared at each public API
   * call boundary. */
  Value apiRoots[BK_API_ROOTS_MAX];
  int apiRootCount;

  /* Host natives registered through the embedding API (brokm_register): name
   * copies the type checker re-registers on every run of this VM. */
  char **hostNatives;
  int hostNativeCount;
  int hostNativeCap;
} VM;

typedef enum {
  BK_OK,
  BK_COMPILE_ERROR,
  BK_RUNTIME_ERROR
} InterpretResult;

/* The current VM instance (v0.11). The runtime is instance-based — create with
 * vm_new(), destroy with vm_destroy() — but the interpreter, GC, allocator, and
 * natives all act on this current-instance pointer rather than threading a
 * parameter, so exactly one VM is active at a time. The public API (api.c)
 * saves/restores it around every entry point, which makes nested cross-VM calls
 * (a host native on VM A driving VM B) behave. One VM per *thread* would need
 * this pointer to be thread-local and the JIT page pool to be per-VM or locked
 * — out of scope for now. */
extern VM *vm;

/* Create a fresh, independent VM (own heap, globals, interned strings, GC) and
 * make it current. Returns NULL on allocation failure. */
VM *vm_new(void);
/* Free target and everything it owns. If target was current, current becomes
 * NULL. */
void vm_destroy(VM *target);
InterpretResult vm_interpret(const char *source);
/* Like vm_interpret, but resolves relative `#include` paths against baseDir
 * (the directory of the file being run). */
InterpretResult vm_interpret_file(const char *source, const char *baseDir);

void vm_push(Value value);
Value vm_pop(void);
void vm_define_native(const char *name, NativeFn fn);

/* Embedding support (api.c). vm_invoke expects [callee, args...] on the value
 * stack and leaves the result on top. The API temp-root stack keeps host-created
 * values alive across allocations until the next public call boundary. */
InterpretResult vm_invoke(int argCount);
void vm_api_root(Value value);
void vm_api_clear_roots(void);

#endif /* BROKM_VM_H */
