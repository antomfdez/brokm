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
} VM;

typedef enum {
  BK_OK,
  BK_COMPILE_ERROR,
  BK_RUNTIME_ERROR
} InterpretResult;

/* Single VM instance for v0.1. Multi-instance embedding is a later milestone;
 * object.c and natives.c reference this global directly. */
extern VM vm;

void vm_init(void);
void vm_free(void);
InterpretResult vm_interpret(const char *source);

void vm_push(Value value);
Value vm_pop(void);
void vm_define_native(const char *name, NativeFn fn);

#endif /* BROKM_VM_H */
