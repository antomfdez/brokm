/* lexer.h - converts source text into a stream of tokens. */
#ifndef BROKM_LEXER_H
#define BROKM_LEXER_H

#include "token.h"

void lexer_init(const char *source);
Token lexer_next(void);

#endif /* BROKM_LEXER_H */
