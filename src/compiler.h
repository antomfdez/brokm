/* compiler.h - lowers the AST into a bytecode-bearing script function. */
#ifndef BROKM_COMPILER_H
#define BROKM_COMPILER_H

#include "ast.h"
#include "object.h"

/* Compile a parsed program into the top-level <script> function. Returns NULL
 * if a compile error occurred (reported to stderr). */
ObjFunction *compile_program(StmtList *program);

#endif /* BROKM_COMPILER_H */
