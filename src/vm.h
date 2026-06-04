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
  Table strings; /* interned string set */
  Obj *objects;  /* intrusive list of every heap object */
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
