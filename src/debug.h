/* debug.h - bytecode disassembler for diagnostics. */
#ifndef BROKM_DEBUG_H
#define BROKM_DEBUG_H

#include "chunk.h"

void disassemble_chunk(Chunk *chunk, const char *name);
int disassemble_instruction(Chunk *chunk, int offset);

#endif /* BROKM_DEBUG_H */
