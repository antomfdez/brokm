/* chunk.h - a chunk of bytecode plus its constant pool and line table. */
#ifndef BROKM_CHUNK_H
#define BROKM_CHUNK_H

#include "common.h"
#include "value.h"

typedef enum {
  OP_CONSTANT,      /* operand: constant index */
  OP_NIL,
  OP_TRUE,
  OP_FALSE,
  OP_POP,
  OP_DEFINE_GLOBAL, /* operand: name constant index */
  OP_GET_GLOBAL,    /* operand: name constant index */
  OP_SET_GLOBAL,    /* operand: name constant index */
  OP_GET_LOCAL,     /* operand: slot */
  OP_SET_LOCAL,     /* operand: slot */
  OP_EQUAL,
  OP_NOT_EQUAL,
  OP_GREATER,
  OP_GREATER_EQUAL,
  OP_LESS,
  OP_LESS_EQUAL,
  OP_ADD,
  OP_SUB,
  OP_MUL,
  OP_DIV,
  OP_MOD,
  OP_NEGATE,
  OP_NOT,
  OP_BIT_AND,
  OP_BIT_OR,
  OP_BIT_XOR,
  OP_BIT_NOT,
  OP_SHL,
  OP_SHR,
  OP_JUMP,           /* operand: 16-bit forward offset */
  OP_JUMP_IF_FALSE,  /* operand: 16-bit forward offset */
  OP_JUMP_IF_TRUE,   /* operand: 16-bit forward offset */
  OP_LOOP,           /* operand: 16-bit backward offset */
  OP_CALL,           /* operand: argument count */
  OP_PRINT,          /* operand: argument count (format + args) */
  OP_ARRAY,          /* operand: element count */
  OP_INDEX_GET,
  OP_INDEX_SET,
  OP_GET_FIELD,      /* operand: field-name constant index */
  OP_SET_FIELD,      /* operand: field-name constant index */
  OP_INVOKE,         /* operands: method-name constant index, then argument count */
  /* Typed (int-specialized) opcodes (v0.4.1). Emitted when the type checker has
   * proven both operands are TY_INT. They run a branch-lean integer path but
   * carry a runtime IS_INT guard and deopt to their generic counterpart on a
   * miss, since gradual typing lets a float occupy a statically-int slot. */
  OP_IADD,
  OP_ISUB,
  OP_IMUL,
  OP_IDIV,
  OP_IMOD,
  OP_ILESS,
  OP_ILESS_EQUAL,
  OP_IGREATER,
  OP_IGREATER_EQUAL,
  OP_RETURN,
  /* Wide forms with a 16-bit big-endian constant-index operand, emitted when
   * a chunk's pool outgrows the one-byte 256-constant limit (the top-level
   * chunk holds one constant per declared function, so large multi-file
   * programs get there first). Appended at the end of the enum so every
   * existing opcode keeps its value — realcc-emitted bytecode and the
   * byte-identical bootstrap stay valid. */
  OP_CONSTANT_W,      /* operand: 16-bit constant index */
  OP_DEFINE_GLOBAL_W, /* operand: 16-bit name constant index */
  OP_GET_GLOBAL_W,    /* operand: 16-bit name constant index */
  OP_SET_GLOBAL_W,    /* operand: 16-bit name constant index */
  OP_GET_FIELD_W,     /* operand: 16-bit field-name constant index */
  OP_SET_FIELD_W,     /* operand: 16-bit field-name constant index */
  OP_INVOKE_W         /* operands: 16-bit method-name constant index, argument count */
} OpCode;

typedef struct {
  int count;
  int capacity;
  U8 *code;
  int *lines;
  ValueArray constants;
} Chunk;

void chunk_init(Chunk *chunk);
void chunk_free(Chunk *chunk);
void chunk_write(Chunk *chunk, U8 byte, int line);
int chunk_add_constant(Chunk *chunk, Value value);

#endif /* BROKM_CHUNK_H */
