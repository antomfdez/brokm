/* cemit.h - AOT back-end: translate compiled bytecode chunks to C source.
 *
 * cemit_program writes a complete, self-contained C translation unit for the
 * compiled script: one C function per ObjFunction (the interpreter specialized
 * for that chunk, on the same VM value stack via the jit_h_* helper ABI), a
 * bootstrap that rebuilds the constant graph (strings, classes, nested
 * functions), and a main() that runs the script. The output compiles and links
 * against the brokm runtime sources with -DBK_NO_JIT. */
#ifndef BROKM_CEMIT_H
#define BROKM_CEMIT_H

#include <stdio.h>

#include "object.h"

typedef struct {
  bool freestanding; /* emit a translation unit for BK_FREESTANDING */
} CEmitOptions;

/* Returns false (after reporting to stderr) if the program contains a
 * construct the emitter cannot translate. */
bool cemit_program(ObjFunction *script, FILE *out, const CEmitOptions *opt);

#endif /* BROKM_CEMIT_H */
