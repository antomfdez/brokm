/* parser.h - turns tokens into an AST (recursive descent + Pratt). */
#ifndef BROKM_PARSER_H
#define BROKM_PARSER_H

#include "ast.h"

/* Parse `source` into a top-level statement list. `baseDir` resolves relative
 * `#include` paths (NULL = current directory). Returns false on a syntax error
 * (diagnostics are printed to stderr). On failure `out` may hold a partial
 * tree, which the caller should still free. */
bool parse(const char *source, const char *baseDir, StmtList *out);

#endif /* BROKM_PARSER_H */
