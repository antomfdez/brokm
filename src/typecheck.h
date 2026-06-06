/* typecheck.h - the v0.4 static type-checking pass.
 *
 * Runs over the AST between parse and bytecode compilation. It carries the
 * HolyC type annotations through the tree, infers expression types, and reports
 * type errors with line numbers. brokm uses gradual typing: anything the pass
 * cannot reason about precisely is TY_UNKNOWN and never wrongly rejected. */
#ifndef BROKM_TYPECHECK_H
#define BROKM_TYPECHECK_H

#include "ast.h"

/* Returns true if the program type-checks; false (with errors on stderr) if
 * any type error was found. */
bool typecheck(StmtList *program);

/* Register a host-provided native name (from the embedding API) so scripts may
 * call it without an "unknown identifier" error. Persists across typecheck()
 * runs; treated as a variadic, TY_UNKNOWN builtin like the standard natives. */
void typecheck_register_native(const char *name);

#endif /* BROKM_TYPECHECK_H */
