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
  OP_RETURN
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
