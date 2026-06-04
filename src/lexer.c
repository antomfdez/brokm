/* lexer.c - hand-written scanner for HolyC-flavored brokm source. */
#include <string.h>

#include "common.h"
#include "lexer.h"

typedef struct {
  const char *start;   /* start of the token being scanned */
  const char *current; /* current scan position */
  int line;
} Lexer;

static Lexer lexer;

void lexer_init(const char *source) {
  lexer.start = source;
  lexer.current = source;
  lexer.line = 1;
}

static bool is_alpha(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool is_digit(char c) { return c >= '0' && c <= '9'; }

static bool is_hex(char c) {
  return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static bool at_end(void) { return *lexer.current == '\0'; }

static char advance(void) { return *lexer.current++; }

static char peek(void) { return *lexer.current; }

static char peek_next(void) {
  if (at_end()) return '\0';
  return lexer.current[1];
}

static bool match(char expected) {
  if (at_end() || *lexer.current != expected) return false;
  lexer.current++;
  return true;
}

static Token make_token(TokenType type) {
  Token token;
  token.type = type;
  token.start = lexer.start;
  token.length = (int)(lexer.current - lexer.start);
  token.line = lexer.line;
  return token;
}

static Token error_token(const char *message) {
  Token token;
  token.type = TOKEN_ERROR;
  token.start = message;
  token.length = (int)strlen(message);
  token.line = lexer.line;
  return token;
}

static void skip_whitespace(void) {
  for (;;) {
    char c = peek();
    switch (c) {
      case ' ':
      case '\r':
      case '\t':
        advance();
        break;
      case '\n':
        lexer.line++;
        advance();
        break;
      case '/':
        if (peek_next() == '/') {
          while (peek() != '\n' && !at_end()) advance();
        } else if (peek_next() == '*') {
          advance();
          advance();
          while (!at_end() && !(peek() == '*' && peek_next() == '/')) {
            if (peek() == '\n') lexer.line++;
            advance();
          }
          if (!at_end()) {
            advance(); /* '*' */
            advance(); /* '/' */
          }
        } else {
          return;
        }
        break;
      default:
        return;
    }
  }
}

typedef struct {
  const char *word;
  TokenType type;
} Keyword;

static const Keyword KEYWORDS[] = {
    {"U0", TOKEN_TYPE},   {"U8", TOKEN_TYPE},   {"U16", TOKEN_TYPE},
    {"U32", TOKEN_TYPE},  {"U64", TOKEN_TYPE},  {"I8", TOKEN_TYPE},
    {"I16", TOKEN_TYPE},  {"I32", TOKEN_TYPE},  {"I64", TOKEN_TYPE},
    {"F64", TOKEN_TYPE},  {"Bool", TOKEN_TYPE},
    {"if", TOKEN_IF},     {"else", TOKEN_ELSE}, {"while", TOKEN_WHILE},
    {"for", TOKEN_FOR},   {"do", TOKEN_DO},     {"return", TOKEN_RETURN},
    {"switch", TOKEN_SWITCH}, {"case", TOKEN_CASE}, {"default", TOKEN_DEFAULT},
    {"break", TOKEN_BREAK}, {"continue", TOKEN_CONTINUE},
    {"class", TOKEN_CLASS}, {"struct", TOKEN_CLASS},
    {"TRUE", TOKEN_TRUE}, {"FALSE", TOKEN_FALSE}, {"NULL", TOKEN_NIL},
};

static TokenType identifier_type(void) {
  int length = (int)(lexer.current - lexer.start);
  size_t count = sizeof(KEYWORDS) / sizeof(KEYWORDS[0]);
  for (size_t i = 0; i < count; i++) {
    const Keyword *kw = &KEYWORDS[i];
    if ((int)strlen(kw->word) == length &&
        memcmp(lexer.start, kw->word, (size_t)length) == 0) {
      return kw->type;
    }
  }
  return TOKEN_IDENTIFIER;
}

static Token identifier(void) {
  while (is_alpha(peek()) || is_digit(peek())) advance();
  return make_token(identifier_type());
}

static Token number(void) {
  if (lexer.start[0] == '0' && (peek() == 'x' || peek() == 'X')) {
    advance();
    while (is_hex(peek())) advance();
    return make_token(TOKEN_INT);
  }

  while (is_digit(peek())) advance();

  bool isFloat = false;
  if (peek() == '.' && is_digit(peek_next())) {
    isFloat = true;
    advance();
    while (is_digit(peek())) advance();
  }
  if (peek() == 'e' || peek() == 'E') {
    isFloat = true;
    advance();
    if (peek() == '+' || peek() == '-') advance();
    while (is_digit(peek())) advance();
  }
  return make_token(isFloat ? TOKEN_FLOAT : TOKEN_INT);
}

static Token string(void) {
  while (peek() != '"' && !at_end()) {
    if (peek() == '\n') lexer.line++;
    if (peek() == '\\' && !at_end()) advance(); /* skip escaped char */
    advance();
  }
  if (at_end()) return error_token("Unterminated string.");
  advance(); /* closing quote */
  return make_token(TOKEN_STRING);
}

static Token character(void) {
  /* A char literal yields an integer code; the parser decodes the lexeme. */
  while (peek() != '\'' && !at_end()) {
    if (peek() == '\n') lexer.line++;
    if (peek() == '\\' && !at_end()) advance();
    advance();
  }
  if (at_end()) return error_token("Unterminated character literal.");
  advance(); /* closing quote */
  return make_token(TOKEN_INT);
}

Token lexer_next(void) {
  skip_whitespace();
  lexer.start = lexer.current;
  if (at_end()) return make_token(TOKEN_EOF);

  char c = advance();
  if (is_alpha(c)) return identifier();
  if (is_digit(c)) return number();

  switch (c) {
    case '(': return make_token(TOKEN_LPAREN);
    case ')': return make_token(TOKEN_RPAREN);
    case '{': return make_token(TOKEN_LBRACE);
    case '}': return make_token(TOKEN_RBRACE);
    case '[': return make_token(TOKEN_LBRACKET);
    case ']': return make_token(TOKEN_RBRACKET);
    case ',': return make_token(TOKEN_COMMA);
    case ';': return make_token(TOKEN_SEMICOLON);
    case ':': return make_token(TOKEN_COLON);
    case '.': return make_token(TOKEN_DOT);
    case '~': return make_token(TOKEN_TILDE);
    case '"': return string();
    case '\'': return character();
    case '+':
      if (match('+')) return make_token(TOKEN_PLUS_PLUS);
      if (match('=')) return make_token(TOKEN_PLUS_EQ);
      return make_token(TOKEN_PLUS);
    case '-':
      if (match('-')) return make_token(TOKEN_MINUS_MINUS);
      if (match('=')) return make_token(TOKEN_MINUS_EQ);
      return make_token(TOKEN_MINUS);
    case '*':
      if (match('=')) return make_token(TOKEN_STAR_EQ);
      return make_token(TOKEN_STAR);
    case '/':
      if (match('=')) return make_token(TOKEN_SLASH_EQ);
      return make_token(TOKEN_SLASH);
    case '%':
      if (match('=')) return make_token(TOKEN_PERCENT_EQ);
      return make_token(TOKEN_PERCENT);
    case '!':
      return make_token(match('=') ? TOKEN_BANG_EQ : TOKEN_BANG);
    case '=':
      return make_token(match('=') ? TOKEN_EQ_EQ : TOKEN_EQ);
    case '^':
      if (match('=')) return make_token(TOKEN_CARET_EQ);
      return make_token(TOKEN_CARET);
    case '<':
      if (match('=')) return make_token(TOKEN_LESS_EQ);
      if (match('<')) return make_token(match('=') ? TOKEN_SHL_EQ : TOKEN_SHL);
      return make_token(TOKEN_LESS);
    case '>':
      if (match('=')) return make_token(TOKEN_GREATER_EQ);
      if (match('>')) return make_token(match('=') ? TOKEN_SHR_EQ : TOKEN_SHR);
      return make_token(TOKEN_GREATER);
    case '&':
      if (match('&')) return make_token(TOKEN_AMP_AMP);
      if (match('=')) return make_token(TOKEN_AMP_EQ);
      return make_token(TOKEN_AMP);
    case '|':
      if (match('|')) return make_token(TOKEN_PIPE_PIPE);
      if (match('=')) return make_token(TOKEN_PIPE_EQ);
      return make_token(TOKEN_PIPE);
  }

  return error_token("Unexpected character.");
}
