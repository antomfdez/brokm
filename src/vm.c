/* vm.c - bytecode interpreter: the heart of brokm v0.1. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler.h"
#include "gc.h"
#include "jit.h"
#include "memory.h"
#include "natives.h"
#include "object.h"
#include "parser.h"
#include "table.h"
#include "typecheck.h"
#include "value.h"
#include "vm.h"

#ifdef BK_DEBUG_TRACE_EXECUTION
#include "debug.h"
#endif

VM vm;

static void reset_stack(void) {
  vm.stackTop = vm.stack;
  vm.frameCount = 0;
}

static void runtime_error(const char *format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fputs("\n", stderr);

  for (int i = vm.frameCount - 1; i >= 0; i--) {
    CallFrame *frame = &vm.frames[i];
    ObjFunction *fn = frame->function;
    size_t instruction = (size_t)(frame->ip - fn->chunk.code - 1);
    fprintf(stderr, "[line %d] in %s\n", fn->chunk.lines[instruction],
            fn->name != NULL ? fn->name->chars : "<script>");
  }
  reset_stack();
}

void vm_push(Value value) { *vm.stackTop++ = value; }
Value vm_pop(void) { return *(--vm.stackTop); }
static Value peek(int distance) { return vm.stackTop[-1 - distance]; }

void vm_init(void) {
  reset_stack();
  vm.objects = NULL;

  vm.gcEnabled = false; /* enabled once compilation is done */
  vm.bytesAllocated = 0;
  vm.nextGC = 1 << 20;
  vm.bytesSinceMinor = 0;
  vm.minorThreshold = 1 << 18; /* 256 KiB */
  vm.grayCount = 0;
  vm.grayCapacity = 0;
  vm.grayStack = NULL;
  vm.rememberedCount = 0;
  vm.rememberedCapacity = 0;
  vm.rememberedSet = NULL;

  table_init(&vm.globals);
  table_init(&vm.strings);
  natives_register();
  jit_init();
}

void vm_free(void) {
  jit_shutdown();
  table_free(&vm.globals);
  table_free(&vm.strings);
  objects_free_all();
  free(vm.grayStack);
  free(vm.rememberedSet);
}

void vm_define_native(const char *name, NativeFn fn) {
  ObjString *n = string_copy(name, (int)strlen(name));
  vm_push(OBJ_VAL(n));
  ObjNative *native = native_new(fn, n);
  vm_push(OBJ_VAL(native));
  table_set(&vm.globals, n, OBJ_VAL(native));
  vm_pop();
  vm_pop();
}

static bool call_function(ObjFunction *fn, int argCount) {
  if (argCount != fn->arity) {
    runtime_error("%s expected %d arguments but got %d.",
                  fn->name ? fn->name->chars : "<script>", fn->arity, argCount);
    return false;
  }
#ifndef BK_NO_JIT
  /* Profile-gated JIT: compile once hot, then run the native code directly.
   * Native functions do not use vm.frames — their generated epilogue leaves the
   * result on the value stack just as OP_RETURN would. */
  if (fn->nativeCode == NULL && !fn->jitDisabled &&
      ++fn->callCount >= jit_threshold()) {
    jit_try_compile(fn);
  }
  if (fn->nativeCode != NULL) {
    if (vm.stackTop - vm.stack > BK_STACK_MAX - BK_SLOT_COUNT) {
      runtime_error("Stack overflow.");
      return false;
    }
    return ((JitFn)fn->nativeCode)(vm.stackTop - argCount - 1);
  }
#endif
  if (vm.frameCount == BK_FRAMES_MAX) {
    runtime_error("Stack overflow.");
    return false;
  }
  CallFrame *frame = &vm.frames[vm.frameCount++];
  frame->function = fn;
  frame->ip = fn->chunk.code;
  frame->slots = vm.stackTop - argCount - 1;
  return true;
}

static bool call_value(Value callee, int argCount) {
  if (IS_OBJ(callee)) {
    switch (OBJ_TYPE(callee)) {
      case OBJ_FUNCTION:
        return call_function(AS_FUNCTION(callee), argCount);
      case OBJ_NATIVE: {
        NativeFn native = AS_NATIVE(callee)->fn;
        Value result = native(argCount, vm.stackTop - argCount);
        vm.stackTop -= argCount + 1;
        vm_push(result);
        return true;
      }
      case OBJ_CLASS: {
        ObjClass *klass = AS_CLASS(callee);
        if (argCount > klass->fieldCount) {
          runtime_error("%s takes at most %d fields but got %d.",
                        klass->name->chars, klass->fieldCount, argCount);
          return false;
        }
        ObjInstance *inst = instance_new(klass);
        vm_push(OBJ_VAL(inst)); /* protect across field-table growth */
        for (int i = 0; i < klass->fieldCount; i++) {
          Value v = (i < argCount) ? vm.stackTop[-1 - argCount + i] : NIL_VAL;
          table_set(&inst->fields, klass->fields[i], v);
        }
        vm.stackTop -= argCount + 2; /* drop inst, args, and callee */
        vm_push(OBJ_VAL(inst));
        return true;
      }
      default:
        break;
    }
  }
  runtime_error("Can only call functions.");
  return false;
}

typedef enum { AR_OK, AR_NOT_NUM, AR_NOT_INT, AR_DIV0 } ArStatus;

static ArStatus apply_binary(U8 op, Value a, Value b, Value *out) {
  if (!IS_NUMBER(a) || !IS_NUMBER(b)) return AR_NOT_NUM;
  bool bothInt = IS_INT(a) && IS_INT(b);
  switch (op) {
    case OP_ADD:
      *out = bothInt ? INT_VAL(AS_INT(a) + AS_INT(b))
                     : FLOAT_VAL(AS_F64(a) + AS_F64(b));
      return AR_OK;
    case OP_SUB:
      *out = bothInt ? INT_VAL(AS_INT(a) - AS_INT(b))
                     : FLOAT_VAL(AS_F64(a) - AS_F64(b));
      return AR_OK;
    case OP_MUL:
      *out = bothInt ? INT_VAL(AS_INT(a) * AS_INT(b))
                     : FLOAT_VAL(AS_F64(a) * AS_F64(b));
      return AR_OK;
    case OP_DIV:
      if (bothInt) {
        if (AS_INT(b) == 0) return AR_DIV0;
        *out = INT_VAL(AS_INT(a) / AS_INT(b));
      } else {
        *out = FLOAT_VAL(AS_F64(a) / AS_F64(b));
      }
      return AR_OK;
    case OP_MOD:
      if (!bothInt) return AR_NOT_INT;
      if (AS_INT(b) == 0) return AR_DIV0;
      *out = INT_VAL(AS_INT(a) % AS_INT(b));
      return AR_OK;
    case OP_LESS:
      *out = BOOL_VAL(bothInt ? AS_INT(a) < AS_INT(b) : AS_F64(a) < AS_F64(b));
      return AR_OK;
    case OP_LESS_EQUAL:
      *out = BOOL_VAL(bothInt ? AS_INT(a) <= AS_INT(b) : AS_F64(a) <= AS_F64(b));
      return AR_OK;
    case OP_GREATER:
      *out = BOOL_VAL(bothInt ? AS_INT(a) > AS_INT(b) : AS_F64(a) > AS_F64(b));
      return AR_OK;
    case OP_GREATER_EQUAL:
      *out = BOOL_VAL(bothInt ? AS_INT(a) >= AS_INT(b) : AS_F64(a) >= AS_F64(b));
      return AR_OK;
    case OP_BIT_AND:
      if (!bothInt) return AR_NOT_INT;
      *out = INT_VAL(AS_INT(a) & AS_INT(b));
      return AR_OK;
    case OP_BIT_OR:
      if (!bothInt) return AR_NOT_INT;
      *out = INT_VAL(AS_INT(a) | AS_INT(b));
      return AR_OK;
    case OP_BIT_XOR:
      if (!bothInt) return AR_NOT_INT;
      *out = INT_VAL(AS_INT(a) ^ AS_INT(b));
      return AR_OK;
    case OP_SHL:
      if (!bothInt) return AR_NOT_INT;
      *out = INT_VAL(AS_INT(a) << AS_INT(b));
      return AR_OK;
    case OP_SHR:
      if (!bothInt) return AR_NOT_INT;
      *out = INT_VAL(AS_INT(a) >> AS_INT(b));
      return AR_OK;
    default:
      return AR_NOT_NUM;
  }
}

/* Build a + b for two strings. The operands stay on the stack (peeked, not
 * popped) so the collector keeps them alive across the allocation. */
static void concatenate(void) {
  ObjString *b = AS_STRING(peek(0));
  ObjString *a = AS_STRING(peek(1));
  int length = a->length + b->length;
  char *chars = ALLOCATE(char, length + 1);
  memcpy(chars, a->chars, (size_t)a->length);
  memcpy(chars + a->length, b->chars, (size_t)b->length);
  chars[length] = '\0';
  ObjString *result = string_take(chars, length);
  vm_pop();
  vm_pop();
  vm_push(OBJ_VAL(result));
}

static bool do_binary(U8 op) {
  Value b = vm_pop();
  Value a = vm_pop();
  Value out;
  switch (apply_binary(op, a, b, &out)) {
    case AR_OK:
      vm_push(out);
      return true;
    case AR_DIV0:
      runtime_error("Division by zero.");
      return false;
    case AR_NOT_INT:
      runtime_error("Operands must be integers.");
      return false;
    default:
      runtime_error("Operands must be numbers.");
      return false;
  }
}

/* Run bytecode until the call stack unwinds back to `baseFrameCount`. The top
 * level passes 0 (run the whole script); the JIT's call helper passes the frame
 * count it saw before a bytecode callee was pushed, so it executes exactly that
 * nested call and returns control to the native code. */
static InterpretResult run_until(int baseFrameCount) {
  CallFrame *frame = &vm.frames[vm.frameCount - 1];

#define READ_BYTE() (*frame->ip++)
#define READ_SHORT() \
  (frame->ip += 2, (U16)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONSTANT() (frame->function->chunk.constants.values[READ_BYTE()])
#define READ_STRING() AS_STRING(READ_CONSTANT())
#define BINARY(op)                       \
  do {                                   \
    if (!do_binary(op)) return BK_RUNTIME_ERROR; \
  } while (0)

  for (;;) {
#ifdef BK_DEBUG_TRACE_EXECUTION
    printf("          ");
    for (Value *slot = vm.stack; slot < vm.stackTop; slot++) {
      printf("[ ");
      value_print(*slot);
      printf(" ]");
    }
    printf("\n");
    disassemble_instruction(&frame->function->chunk,
                            (int)(frame->ip - frame->function->chunk.code));
#endif

    U8 instruction = READ_BYTE();
    switch (instruction) {
      case OP_CONSTANT: vm_push(READ_CONSTANT()); break;
      case OP_NIL: vm_push(NIL_VAL); break;
      case OP_TRUE: vm_push(BOOL_VAL(true)); break;
      case OP_FALSE: vm_push(BOOL_VAL(false)); break;
      case OP_POP: vm_pop(); break;

      case OP_DEFINE_GLOBAL: {
        ObjString *name = READ_STRING();
        table_set(&vm.globals, name, peek(0));
        vm_pop();
        break;
      }
      case OP_GET_GLOBAL: {
        ObjString *name = READ_STRING();
        Value value;
        if (!table_get(&vm.globals, name, &value)) {
          runtime_error("Undefined variable '%s'.", name->chars);
          return BK_RUNTIME_ERROR;
        }
        vm_push(value);
        break;
      }
      case OP_SET_GLOBAL: {
        ObjString *name = READ_STRING();
        if (table_set(&vm.globals, name, peek(0))) {
          table_delete(&vm.globals, name);
          runtime_error("Undefined variable '%s'.", name->chars);
          return BK_RUNTIME_ERROR;
        }
        break;
      }
      case OP_GET_LOCAL: {
        U8 slot = READ_BYTE();
        vm_push(frame->slots[slot]);
        break;
      }
      case OP_SET_LOCAL: {
        U8 slot = READ_BYTE();
        frame->slots[slot] = peek(0);
        break;
      }

      case OP_EQUAL: {
        Value b = vm_pop(), a = vm_pop();
        vm_push(BOOL_VAL(value_equal(a, b)));
        break;
      }
      case OP_NOT_EQUAL: {
        Value b = vm_pop(), a = vm_pop();
        vm_push(BOOL_VAL(!value_equal(a, b)));
        break;
      }
      case OP_GREATER: op_greater: BINARY(OP_GREATER); break;
      case OP_GREATER_EQUAL: op_greater_equal: BINARY(OP_GREATER_EQUAL); break;
      case OP_LESS: op_less: BINARY(OP_LESS); break;
      case OP_LESS_EQUAL: op_less_equal: BINARY(OP_LESS_EQUAL); break;
      case OP_ADD: op_add:
        if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
          concatenate();
        } else if (!do_binary(OP_ADD)) {
          return BK_RUNTIME_ERROR;
        }
        break;
      case OP_SUB: op_sub: BINARY(OP_SUB); break;
      case OP_MUL: op_mul: BINARY(OP_MUL); break;
      case OP_DIV: op_div: BINARY(OP_DIV); break;
      case OP_MOD: op_mod: BINARY(OP_MOD); break;

      /* Int-specialized fast paths (v0.4.1). Both operands are statically
       * TY_INT, but gradual typing allows a float to occupy an int slot, so we
       * guard on IS_INT and deopt to the generic opcode on a miss. */
      case OP_IADD:
        if (IS_INT(peek(0)) && IS_INT(peek(1))) {
          Value b = vm_pop(), a = vm_pop();
          vm_push(INT_VAL(AS_INT(a) + AS_INT(b)));
          break;
        }
        goto op_add;
      case OP_ISUB:
        if (IS_INT(peek(0)) && IS_INT(peek(1))) {
          Value b = vm_pop(), a = vm_pop();
          vm_push(INT_VAL(AS_INT(a) - AS_INT(b)));
          break;
        }
        goto op_sub;
      case OP_IMUL:
        if (IS_INT(peek(0)) && IS_INT(peek(1))) {
          Value b = vm_pop(), a = vm_pop();
          vm_push(INT_VAL(AS_INT(a) * AS_INT(b)));
          break;
        }
        goto op_mul;
      case OP_IDIV:
        if (IS_INT(peek(0)) && IS_INT(peek(1)) && AS_INT(peek(0)) != 0) {
          Value b = vm_pop(), a = vm_pop();
          vm_push(INT_VAL(AS_INT(a) / AS_INT(b)));
          break;
        }
        goto op_div; /* non-int or divide-by-zero: generic path reports it */
      case OP_IMOD:
        if (IS_INT(peek(0)) && IS_INT(peek(1)) && AS_INT(peek(0)) != 0) {
          Value b = vm_pop(), a = vm_pop();
          vm_push(INT_VAL(AS_INT(a) % AS_INT(b)));
          break;
        }
        goto op_mod;
      case OP_ILESS:
        if (IS_INT(peek(0)) && IS_INT(peek(1))) {
          Value b = vm_pop(), a = vm_pop();
          vm_push(BOOL_VAL(AS_INT(a) < AS_INT(b)));
          break;
        }
        goto op_less;
      case OP_ILESS_EQUAL:
        if (IS_INT(peek(0)) && IS_INT(peek(1))) {
          Value b = vm_pop(), a = vm_pop();
          vm_push(BOOL_VAL(AS_INT(a) <= AS_INT(b)));
          break;
        }
        goto op_less_equal;
      case OP_IGREATER:
        if (IS_INT(peek(0)) && IS_INT(peek(1))) {
          Value b = vm_pop(), a = vm_pop();
          vm_push(BOOL_VAL(AS_INT(a) > AS_INT(b)));
          break;
        }
        goto op_greater;
      case OP_IGREATER_EQUAL:
        if (IS_INT(peek(0)) && IS_INT(peek(1))) {
          Value b = vm_pop(), a = vm_pop();
          vm_push(BOOL_VAL(AS_INT(a) >= AS_INT(b)));
          break;
        }
        goto op_greater_equal;
      case OP_BIT_AND: BINARY(OP_BIT_AND); break;
      case OP_BIT_OR: BINARY(OP_BIT_OR); break;
      case OP_BIT_XOR: BINARY(OP_BIT_XOR); break;
      case OP_SHL: BINARY(OP_SHL); break;
      case OP_SHR: BINARY(OP_SHR); break;

      case OP_NEGATE: {
        Value v = vm_pop();
        if (IS_INT(v)) {
          vm_push(INT_VAL(-AS_INT(v)));
        } else if (IS_FLOAT(v)) {
          vm_push(FLOAT_VAL(-AS_FLOAT(v)));
        } else {
          runtime_error("Operand must be a number.");
          return BK_RUNTIME_ERROR;
        }
        break;
      }
      case OP_NOT:
        vm_push(BOOL_VAL(!value_truthy(vm_pop())));
        break;
      case OP_BIT_NOT: {
        Value v = vm_pop();
        if (!IS_INT(v)) {
          runtime_error("Operand must be an integer.");
          return BK_RUNTIME_ERROR;
        }
        vm_push(INT_VAL(~AS_INT(v)));
        break;
      }

      case OP_JUMP: {
        U16 offset = READ_SHORT();
        frame->ip += offset;
        break;
      }
      case OP_JUMP_IF_FALSE: {
        U16 offset = READ_SHORT();
        if (!value_truthy(peek(0))) frame->ip += offset;
        break;
      }
      case OP_JUMP_IF_TRUE: {
        U16 offset = READ_SHORT();
        if (value_truthy(peek(0))) frame->ip += offset;
        break;
      }
      case OP_LOOP: {
        U16 offset = READ_SHORT();
        frame->ip -= offset;
        break;
      }

      case OP_CALL: {
        int argCount = READ_BYTE();
        if (!call_value(peek(argCount), argCount)) return BK_RUNTIME_ERROR;
        frame = &vm.frames[vm.frameCount - 1];
        break;
      }
      case OP_PRINT: {
        int argCount = READ_BYTE();
        bk_format(argCount, vm.stackTop - argCount);
        vm.stackTop -= argCount;
        break;
      }
      case OP_ARRAY: {
        int n = READ_BYTE();
        ObjArray *array = array_new();
        vm_push(OBJ_VAL(array)); /* keep reachable while appending */
        Value *base = vm.stackTop - n - 1;
        for (int i = 0; i < n; i++) array_append(array, base[i]);
        vm.stackTop = base; /* drop the n elements and the temp array slot */
        vm_push(OBJ_VAL(array));
        break;
      }
      case OP_INDEX_GET: {
        Value indexVal = vm_pop();
        Value arrayVal = vm_pop();
        if (!IS_ARRAY(arrayVal)) {
          runtime_error("Can only index arrays.");
          return BK_RUNTIME_ERROR;
        }
        if (!IS_INT(indexVal)) {
          runtime_error("Array index must be an integer.");
          return BK_RUNTIME_ERROR;
        }
        ObjArray *array = AS_ARRAY(arrayVal);
        I64 i = AS_INT(indexVal);
        if (i < 0 || i >= array->elements.count) {
          runtime_error("Array index %lld out of bounds (length %d).",
                        (long long)i, array->elements.count);
          return BK_RUNTIME_ERROR;
        }
        vm_push(array->elements.values[i]);
        break;
      }
      case OP_INDEX_SET: {
        Value value = vm_pop();
        Value indexVal = vm_pop();
        Value arrayVal = vm_pop();
        if (!IS_ARRAY(arrayVal)) {
          runtime_error("Can only index arrays.");
          return BK_RUNTIME_ERROR;
        }
        if (!IS_INT(indexVal)) {
          runtime_error("Array index must be an integer.");
          return BK_RUNTIME_ERROR;
        }
        ObjArray *array = AS_ARRAY(arrayVal);
        I64 i = AS_INT(indexVal);
        if (i < 0 || i >= array->elements.count) {
          runtime_error("Array index %lld out of bounds (length %d).",
                        (long long)i, array->elements.count);
          return BK_RUNTIME_ERROR;
        }
        array->elements.values[i] = value;
        gc_write_barrier((Obj *)array, value); /* may be old->young */
        vm_push(value);
        break;
      }
      case OP_GET_FIELD: {
        ObjString *name = READ_STRING();
        Value objVal = vm_pop();
        if (!IS_INSTANCE(objVal)) {
          runtime_error("Only instances have fields.");
          return BK_RUNTIME_ERROR;
        }
        ObjInstance *inst = AS_INSTANCE(objVal);
        Value value;
        if (!table_get(&inst->fields, name, &value)) {
          runtime_error("%s has no field '%s'.", inst->klass->name->chars,
                        name->chars);
          return BK_RUNTIME_ERROR;
        }
        vm_push(value);
        break;
      }
      case OP_SET_FIELD: {
        ObjString *name = READ_STRING();
        Value value = peek(0);
        Value objVal = peek(1);
        if (!IS_INSTANCE(objVal)) {
          runtime_error("Only instances have fields.");
          return BK_RUNTIME_ERROR;
        }
        ObjInstance *inst = AS_INSTANCE(objVal);
        Value existing;
        if (!table_get(&inst->fields, name, &existing)) {
          runtime_error("%s has no field '%s'.", inst->klass->name->chars,
                        name->chars);
          return BK_RUNTIME_ERROR;
        }
        /* both operands stay on the stack across the (possibly collecting) set */
        table_set(&inst->fields, name, value);
        gc_write_barrier((Obj *)inst, value);
        vm_pop(); /* value */
        vm_pop(); /* instance */
        vm_push(value);
        break;
      }
      case OP_RETURN: {
        Value result = vm_pop();
        vm.frameCount--;
        if (vm.frameCount == 0) {
          vm_pop(); /* the <script> function */
          return BK_OK;
        }
        vm.stackTop = frame->slots;
        vm_push(result);
        if (vm.frameCount == baseFrameCount) {
          return BK_OK; /* re-entrant runner: hand the result back to the caller */
        }
        frame = &vm.frames[vm.frameCount - 1];
        break;
      }
    }
  }

#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef READ_STRING
#undef BINARY
}

#ifndef BK_NO_JIT
/* ---- JIT call-backs ----------------------------------------------------
 * Operations the JIT does not inline call these, which run the interpreter's
 * own logic on the same VM value stack as the matching opcodes. The generated
 * code flushes its cached stack-top to vm.stackTop before calling and reloads
 * it after. Each returns false (after reporting) on a runtime error. Declared
 * in jit.h; the generated code bakes their addresses. */

bool jit_h_binary(U8 op) {
  if (op == OP_ADD && IS_STRING(peek(0)) && IS_STRING(peek(1))) {
    concatenate();
    return true;
  }
  return do_binary(op);
}

bool jit_h_equal(int negate) {
  Value b = vm_pop(), a = vm_pop();
  bool eq = value_equal(a, b);
  vm_push(BOOL_VAL(negate ? !eq : eq));
  return true;
}

bool jit_h_get_global(ObjString *name) {
  Value value;
  if (!table_get(&vm.globals, name, &value)) {
    runtime_error("Undefined variable '%s'.", name->chars);
    return false;
  }
  vm_push(value);
  return true;
}

bool jit_h_set_global(ObjString *name) {
  if (table_set(&vm.globals, name, peek(0))) {
    table_delete(&vm.globals, name);
    runtime_error("Undefined variable '%s'.", name->chars);
    return false;
  }
  return true;
}

bool jit_h_call(int argCount) {
  int saved = vm.frameCount;
  if (!call_value(peek(argCount), argCount)) return false;
  if (vm.frameCount > saved) {
    /* a bytecode frame was pushed: run exactly that nested call */
    return run_until(saved) == BK_OK;
  }
  return true; /* native / native-C / class already produced the result */
}

bool jit_h_print(int argCount) {
  bk_format(argCount, vm.stackTop - argCount);
  vm.stackTop -= argCount;
  return true;
}
#endif /* BK_NO_JIT */

InterpretResult vm_interpret(const char *source) {
  /* Parsing and compilation allocate objects (interned names, functions,
   * constants) that are not yet reachable from VM roots, so collection stays
   * off until the script is built and pushed. */
  vm.gcEnabled = false;

  StmtList program;
  bool ok = parse(source, &program);
  if (!ok) {
    free_stmtlist(&program);
    return BK_COMPILE_ERROR;
  }

  if (!typecheck(&program)) {
    free_stmtlist(&program);
    return BK_COMPILE_ERROR;
  }

  ObjFunction *script = compile_program(&program);
  free_stmtlist(&program);
  if (script == NULL) return BK_COMPILE_ERROR;

  /* The top-level script has a bespoke invocation (call_function then
   * run_until below), so it must stay on the interpreter — never JIT it. */
  script->jitDisabled = true;

  vm_push(OBJ_VAL(script));
  call_function(script, 0);

  vm.gcEnabled = true; /* the script and its constants are now rooted */
  return run_until(0);
}
