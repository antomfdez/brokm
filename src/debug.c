/* debug.c - human-readable bytecode dumps. */
#include <stdio.h>

#include "debug.h"
#include "value.h"

static int simple_instruction(const char *name, int offset) {
  printf("%s\n", name);
  return offset + 1;
}

static int constant_instruction(const char *name, Chunk *chunk, int offset) {
  U8 index = chunk->code[offset + 1];
  printf("%-16s %4d '", name, index);
  value_print(chunk->constants.values[index]);
  printf("'\n");
  return offset + 2;
}

static int byte_instruction(const char *name, Chunk *chunk, int offset) {
  U8 slot = chunk->code[offset + 1];
  printf("%-16s %4d\n", name, slot);
  return offset + 2;
}

static int invoke_instruction(const char *name, Chunk *chunk, int offset) {
  U8 index = chunk->code[offset + 1];
  U8 argc = chunk->code[offset + 2];
  printf("%-16s %4d '", name, index);
  value_print(chunk->constants.values[index]);
  printf("' (%d args)\n", argc);
  return offset + 3;
}

static int jump_instruction(const char *name, int sign, Chunk *chunk,
                            int offset) {
  U16 jump = (U16)(chunk->code[offset + 1] << 8);
  jump |= chunk->code[offset + 2];
  printf("%-16s %4d -> %d\n", name, offset, offset + 3 + sign * (int)jump);
  return offset + 3;
}

int disassemble_instruction(Chunk *chunk, int offset) {
  printf("%04d ", offset);
  if (offset > 0 && chunk->lines[offset] == chunk->lines[offset - 1]) {
    printf("   | ");
  } else {
    printf("%4d ", chunk->lines[offset]);
  }

  U8 instruction = chunk->code[offset];
  switch (instruction) {
    case OP_CONSTANT: return constant_instruction("OP_CONSTANT", chunk, offset);
    case OP_NIL: return simple_instruction("OP_NIL", offset);
    case OP_TRUE: return simple_instruction("OP_TRUE", offset);
    case OP_FALSE: return simple_instruction("OP_FALSE", offset);
    case OP_POP: return simple_instruction("OP_POP", offset);
    case OP_DEFINE_GLOBAL:
      return constant_instruction("OP_DEFINE_GLOBAL", chunk, offset);
    case OP_GET_GLOBAL: return constant_instruction("OP_GET_GLOBAL", chunk, offset);
    case OP_SET_GLOBAL: return constant_instruction("OP_SET_GLOBAL", chunk, offset);
    case OP_GET_LOCAL: return byte_instruction("OP_GET_LOCAL", chunk, offset);
    case OP_SET_LOCAL: return byte_instruction("OP_SET_LOCAL", chunk, offset);
    case OP_EQUAL: return simple_instruction("OP_EQUAL", offset);
    case OP_NOT_EQUAL: return simple_instruction("OP_NOT_EQUAL", offset);
    case OP_GREATER: return simple_instruction("OP_GREATER", offset);
    case OP_GREATER_EQUAL: return simple_instruction("OP_GREATER_EQUAL", offset);
    case OP_LESS: return simple_instruction("OP_LESS", offset);
    case OP_LESS_EQUAL: return simple_instruction("OP_LESS_EQUAL", offset);
    case OP_ADD: return simple_instruction("OP_ADD", offset);
    case OP_SUB: return simple_instruction("OP_SUB", offset);
    case OP_MUL: return simple_instruction("OP_MUL", offset);
    case OP_DIV: return simple_instruction("OP_DIV", offset);
    case OP_MOD: return simple_instruction("OP_MOD", offset);
    case OP_NEGATE: return simple_instruction("OP_NEGATE", offset);
    case OP_NOT: return simple_instruction("OP_NOT", offset);
    case OP_BIT_AND: return simple_instruction("OP_BIT_AND", offset);
    case OP_BIT_OR: return simple_instruction("OP_BIT_OR", offset);
    case OP_BIT_XOR: return simple_instruction("OP_BIT_XOR", offset);
    case OP_BIT_NOT: return simple_instruction("OP_BIT_NOT", offset);
    case OP_SHL: return simple_instruction("OP_SHL", offset);
    case OP_SHR: return simple_instruction("OP_SHR", offset);
    case OP_JUMP: return jump_instruction("OP_JUMP", 1, chunk, offset);
    case OP_JUMP_IF_FALSE:
      return jump_instruction("OP_JUMP_IF_FALSE", 1, chunk, offset);
    case OP_JUMP_IF_TRUE:
      return jump_instruction("OP_JUMP_IF_TRUE", 1, chunk, offset);
    case OP_LOOP: return jump_instruction("OP_LOOP", -1, chunk, offset);
    case OP_CALL: return byte_instruction("OP_CALL", chunk, offset);
    case OP_PRINT: return byte_instruction("OP_PRINT", chunk, offset);
    case OP_ARRAY: return byte_instruction("OP_ARRAY", chunk, offset);
    case OP_INDEX_GET: return simple_instruction("OP_INDEX_GET", offset);
    case OP_INDEX_SET: return simple_instruction("OP_INDEX_SET", offset);
    case OP_GET_FIELD: return constant_instruction("OP_GET_FIELD", chunk, offset);
    case OP_SET_FIELD: return constant_instruction("OP_SET_FIELD", chunk, offset);
    case OP_INVOKE: return invoke_instruction("OP_INVOKE", chunk, offset);
    case OP_IADD: return simple_instruction("OP_IADD", offset);
    case OP_ISUB: return simple_instruction("OP_ISUB", offset);
    case OP_IMUL: return simple_instruction("OP_IMUL", offset);
    case OP_IDIV: return simple_instruction("OP_IDIV", offset);
    case OP_IMOD: return simple_instruction("OP_IMOD", offset);
    case OP_ILESS: return simple_instruction("OP_ILESS", offset);
    case OP_ILESS_EQUAL: return simple_instruction("OP_ILESS_EQUAL", offset);
    case OP_IGREATER: return simple_instruction("OP_IGREATER", offset);
    case OP_IGREATER_EQUAL: return simple_instruction("OP_IGREATER_EQUAL", offset);
    case OP_RETURN: return simple_instruction("OP_RETURN", offset);
    default:
      printf("Unknown opcode %d\n", instruction);
      return offset + 1;
  }
}

void disassemble_chunk(Chunk *chunk, const char *name) {
  printf("== %s ==\n", name);
  for (int offset = 0; offset < chunk->count;) {
    offset = disassemble_instruction(chunk, offset);
  }
}
