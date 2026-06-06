/* lexer.h - converts source text into a stream of tokens. */
#ifndef BROKM_LEXER_H
#define BROKM_LEXER_H

#include "token.h"

/* baseDir resolves relative `#include` paths (NULL or "" means the current
 * working directory). */
void lexer_init(const char *source, const char *baseDir);
Token lexer_next(void);

/* Free the file buffers read for #includes. Call after parsing completes (the
 * AST and interned strings no longer reference the source text). */
void lexer_free_buffers(void);

#endif /* BROKM_LEXER_H */
