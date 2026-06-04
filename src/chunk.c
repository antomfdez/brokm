/* chunk.c - bytecode buffer management. */
#include "chunk.h"
#include "memory.h"

void chunk_init(Chunk *chunk) {
  chunk->count = 0;
  chunk->capacity = 0;
  chunk->code = NULL;
  chunk->lines = NULL;
  value_array_init(&chunk->constants);
}

void chunk_free(Chunk *chunk) {
  FREE_ARRAY(U8, chunk->code, chunk->capacity);
  FREE_ARRAY(int, chunk->lines, chunk->capacity);
  value_array_free(&chunk->constants);
  chunk_init(chunk);
}

void chunk_write(Chunk *chunk, U8 byte, int line) {
  if (chunk->capacity < chunk->count + 1) {
    int oldCap = chunk->capacity;
    chunk->capacity = GROW_CAPACITY(oldCap);
    chunk->code = GROW_ARRAY(U8, chunk->code, oldCap, chunk->capacity);
    chunk->lines = GROW_ARRAY(int, chunk->lines, oldCap, chunk->capacity);
  }
  chunk->code[chunk->count] = byte;
  chunk->lines[chunk->count] = line;
  chunk->count++;
}

int chunk_add_constant(Chunk *chunk, Value value) {
  value_array_write(&chunk->constants, value);
  return chunk->constants.count - 1;
}
