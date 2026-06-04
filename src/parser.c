/* parser.c - recursive-descent statements with a Pratt expression core. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "lexer.h"
#include "memory.h"
#include "object.h"
#include "parser.h"

typedef struct {
  Token current;
  Token previous;
  bool hadError;
  bool panicMode;
} Parser;

static Parser parser;

/* ---- error handling --------------------------------------------------- */

static void error_at(Token *token, const char *message) {
  if (parser.panicMode) return;
  parser.panicMode = true;
  fprintf(stderr, "[line %d] Error", token->line);
  if (token->type == TOKEN_EOF) {
    fprintf(stderr, " at end");
  } else if (token->type != TOKEN_ERROR) {
    fprintf(stderr, " at '%.*s'", token->length, token->start);
  }
  fprintf(stderr, ": %s\n", message);
  parser.hadError = true;
}

static void error(const char *message) { error_at(&parser.previous, message); }
static void error_at_current(const char *message) {
  error_at(&parser.current, message);
}

/* ---- token cursor ----------------------------------------------------- */

static void advance(void) {
  parser.previous = parser.current;
  for (;;) {
    parser.current = lexer_next();
    if (parser.current.type != TOKEN_ERROR) break;
    error_at_current(parser.current.start);
  }
}

static bool check(TokenType type) { return parser.current.type == type; }

static bool match(TokenType type) {
  if (!check(type)) return false;
  advance();
  return true;
}

static void consume(TokenType type, const char *message) {
  if (parser.current.type == type) {
    advance();
    return;
  }
  error_at_current(message);
}

/* ---- literal decoding ------------------------------------------------- */

static ObjString *intern_token(Token t) {
  return string_copy(t.start, t.length);
}

static I64 parse_char(Token t) {
  const char *p = t.start + 1; /* skip opening quote */
  if (*p == '\\') {
    p++;
    switch (*p) {
      case 'n': return '\n';
      case 't': return '\t';
      case 'r': return '\r';
      case '0': return '\0';
      case '\\': return '\\';
      case '\'': return '\'';
      case '"': return '"';
      default: return (U8)*p;
    }
  }
  return (U8)*p;
}

static int unescape(const char *src, int n, char *dst) {
  int o = 0;
  for (int i = 0; i < n; i++) {
    char c = src[i];
    if (c == '\\' && i + 1 < n) {
      char e = src[++i];
      switch (e) {
        case 'n': dst[o++] = '\n'; break;
        case 't': dst[o++] = '\t'; break;
        case 'r': dst[o++] = '\r'; break;
        case '0': dst[o++] = '\0'; break;
        case '\\': dst[o++] = '\\'; break;
        case '"': dst[o++] = '"'; break;
        case '\'': dst[o++] = '\''; break;
        default: dst[o++] = e; break;
      }
    } else {
      dst[o++] = c;
    }
  }
  return o;
}

/* ---- AST construction helpers ----------------------------------------- */

static Expr *make_literal(Value v, int line) {
  Expr *e = expr_new(EXPR_LITERAL, line);
  e->as.literal.value = v;
  return e;
}

static Expr *make_variable(ObjString *name, int line) {
  Expr *e = expr_new(EXPR_VARIABLE, line);
  e->as.variable.name = name;
  return e;
}

static Expr *make_binary(TokenType op, Expr *l, Expr *r, int line) {
  Expr *e = expr_new(EXPR_BINARY, line);
  e->as.binary.op = op;
  e->as.binary.left = l;
  e->as.binary.right = r;
  return e;
}

static Expr *make_assign(ObjString *name, Expr *value, int line) {
  Expr *e = expr_new(EXPR_ASSIGN, line);
  e->as.assign.name = name;
  e->as.assign.value = value;
  return e;
}

/* ---- Pratt expression parser ------------------------------------------ */

typedef enum {
  PREC_NONE,
  PREC_ASSIGNMENT,
  PREC_OR,
  PREC_AND,
  PREC_BITOR,
  PREC_BITXOR,
  PREC_BITAND,
  PREC_EQUALITY,
  PREC_COMPARISON,
  PREC_SHIFT,
  PREC_TERM,
  PREC_FACTOR,
  PREC_UNARY,
  PREC_CALL,
  PREC_PRIMARY
} Precedence;

typedef Expr *(*PrefixFn)(bool canAssign);
typedef Expr *(*InfixFn)(bool canAssign, Expr *left);

typedef struct {
  PrefixFn prefix;
  InfixFn infix;
  Precedence precedence;
} ParseRule;

static Expr *parse_precedence(Precedence precedence);
static const ParseRule *get_rule(TokenType type);
static Expr *expression(void);

static Expr *number(bool canAssign) {
  (void)canAssign;
  Token t = parser.previous;
  Value v;
  if (t.type == TOKEN_FLOAT) {
    v = FLOAT_VAL(strtod(t.start, NULL));
  } else if (t.start[0] == '\'') {
    v = INT_VAL(parse_char(t));
  } else if (t.length > 1 && t.start[0] == '0' &&
             (t.start[1] == 'x' || t.start[1] == 'X')) {
    v = INT_VAL((I64)strtoll(t.start, NULL, 16));
  } else {
    v = INT_VAL((I64)strtoll(t.start, NULL, 10));
  }
  return make_literal(v, t.line);
}

static Expr *string(bool canAssign) {
  (void)canAssign;
  Token t = parser.previous;
  int cap = t.length; /* >= decoded length + 1 */
  char *buf = ALLOCATE(char, cap);
  int len = unescape(t.start + 1, t.length - 2, buf);
  buf[len] = '\0';
  ObjString *s = string_take(buf, len);
  return make_literal(OBJ_VAL(s), t.line);
}

static Expr *literal(bool canAssign) {
  (void)canAssign;
  Token t = parser.previous;
  switch (t.type) {
    case TOKEN_TRUE: return make_literal(BOOL_VAL(true), t.line);
    case TOKEN_FALSE: return make_literal(BOOL_VAL(false), t.line);
    case TOKEN_NIL: return make_literal(NIL_VAL, t.line);
    default: return make_literal(NIL_VAL, t.line); /* unreachable */
  }
}

static Expr *grouping(bool canAssign) {
  (void)canAssign;
  Expr *e = expression();
  consume(TOKEN_RPAREN, "Expect ')' after expression.");
  return e;
}

static Expr *unary(bool canAssign) {
  (void)canAssign;
  TokenType op = parser.previous.type;
  int line = parser.previous.line;
  Expr *operand = parse_precedence(PREC_UNARY);
  Expr *e = expr_new(EXPR_UNARY, line);
  e->as.unary.op = op;
  e->as.unary.operand = operand;
  return e;
}

static Expr *pre_incdec(bool canAssign) {
  (void)canAssign;
  TokenType op = parser.previous.type;
  int line = parser.previous.line;
  Expr *operand = parse_precedence(PREC_UNARY);
  if (operand->kind != EXPR_VARIABLE) {
    error("Invalid increment/decrement target.");
    return operand;
  }
  ObjString *name = operand->as.variable.name;
  Expr *one = make_literal(INT_VAL(1), line);
  TokenType base = (op == TOKEN_PLUS_PLUS) ? TOKEN_PLUS : TOKEN_MINUS;
  Expr *bin = make_binary(base, operand, one, line);
  return make_assign(name, bin, line);
}

static bool match_compound(TokenType *baseOp) {
  switch (parser.current.type) {
    case TOKEN_PLUS_EQ: *baseOp = TOKEN_PLUS; break;
    case TOKEN_MINUS_EQ: *baseOp = TOKEN_MINUS; break;
    case TOKEN_STAR_EQ: *baseOp = TOKEN_STAR; break;
    case TOKEN_SLASH_EQ: *baseOp = TOKEN_SLASH; break;
    case TOKEN_PERCENT_EQ: *baseOp = TOKEN_PERCENT; break;
    case TOKEN_AMP_EQ: *baseOp = TOKEN_AMP; break;
    case TOKEN_PIPE_EQ: *baseOp = TOKEN_PIPE; break;
    case TOKEN_CARET_EQ: *baseOp = TOKEN_CARET; break;
    case TOKEN_SHL_EQ: *baseOp = TOKEN_SHL; break;
    case TOKEN_SHR_EQ: *baseOp = TOKEN_SHR; break;
    default: return false;
  }
  advance();
  return true;
}

static Expr *variable(bool canAssign) {
  ObjString *name = intern_token(parser.previous);
  int line = parser.previous.line;
  if (canAssign && match(TOKEN_EQ)) {
    return make_assign(name, expression(), line);
  }
  TokenType base;
  if (canAssign && match_compound(&base)) {
    Expr *rhs = expression();
    Expr *left = make_variable(name, line);
    return make_assign(name, make_binary(base, left, rhs, line), line);
  }
  return make_variable(name, line);
}

static Expr *binary(bool canAssign, Expr *left) {
  (void)canAssign;
  TokenType op = parser.previous.type;
  int line = parser.previous.line;
  const ParseRule *rule = get_rule(op);
  Expr *right = parse_precedence((Precedence)(rule->precedence + 1));
  return make_binary(op, left, right, line);
}

static Expr *logical(bool canAssign, Expr *left) {
  (void)canAssign;
  TokenType op = parser.previous.type;
  int line = parser.previous.line;
  const ParseRule *rule = get_rule(op);
  Expr *right = parse_precedence((Precedence)(rule->precedence + 1));
  Expr *e = expr_new(EXPR_LOGICAL, line);
  e->as.logical.op = op;
  e->as.logical.left = left;
  e->as.logical.right = right;
  return e;
}

static Expr *postfix(bool canAssign, Expr *left) {
  (void)canAssign;
  TokenType op = parser.previous.type;
  int line = parser.previous.line;
  if (left->kind != EXPR_VARIABLE) {
    error("Invalid increment/decrement target.");
    return left;
  }
  ObjString *name = left->as.variable.name;
  Expr *one = make_literal(INT_VAL(1), line);
  TokenType base = (op == TOKEN_PLUS_PLUS) ? TOKEN_PLUS : TOKEN_MINUS;
  return make_assign(name, make_binary(base, left, one, line), line);
}

static Expr *array_literal(bool canAssign) {
  (void)canAssign;
  int line = parser.previous.line;
  Expr *e = expr_new(EXPR_ARRAY, line);
  exprlist_init(&e->as.array.elements);
  if (!check(TOKEN_RBRACKET)) {
    do {
      exprlist_write(&e->as.array.elements, expression());
    } while (match(TOKEN_COMMA));
  }
  consume(TOKEN_RBRACKET, "Expect ']' after array elements.");
  return e;
}

static Expr *index_expr(bool canAssign, Expr *left) {
  int line = parser.previous.line;
  Expr *idx = expression();
  consume(TOKEN_RBRACKET, "Expect ']' after index.");
  if (canAssign && match(TOKEN_EQ)) {
    Expr *e = expr_new(EXPR_INDEX_SET, line);
    e->as.index_set.object = left;
    e->as.index_set.index = idx;
    e->as.index_set.value = expression();
    return e;
  }
  Expr *e = expr_new(EXPR_INDEX, line);
  e->as.index.object = left;
  e->as.index.index = idx;
  return e;
}

static Expr *call(bool canAssign, Expr *left) {
  (void)canAssign;
  int line = parser.previous.line;
  Expr *e = expr_new(EXPR_CALL, line);
  e->as.call.callee = left;
  exprlist_init(&e->as.call.args);
  if (!check(TOKEN_RPAREN)) {
    do {
      exprlist_write(&e->as.call.args, expression());
    } while (match(TOKEN_COMMA));
  }
  consume(TOKEN_RPAREN, "Expect ')' after arguments.");
  return e;
}

static const ParseRule rules[] = {
    [TOKEN_LPAREN] = {grouping, call, PREC_CALL},
    [TOKEN_LBRACKET] = {array_literal, index_expr, PREC_CALL},
    [TOKEN_MINUS] = {unary, binary, PREC_TERM},
    [TOKEN_PLUS] = {NULL, binary, PREC_TERM},
    [TOKEN_STAR] = {NULL, binary, PREC_FACTOR},
    [TOKEN_SLASH] = {NULL, binary, PREC_FACTOR},
    [TOKEN_PERCENT] = {NULL, binary, PREC_FACTOR},
    [TOKEN_PLUS_PLUS] = {pre_incdec, postfix, PREC_CALL},
    [TOKEN_MINUS_MINUS] = {pre_incdec, postfix, PREC_CALL},
    [TOKEN_BANG] = {unary, NULL, PREC_NONE},
    [TOKEN_TILDE] = {unary, NULL, PREC_NONE},
    [TOKEN_EQ_EQ] = {NULL, binary, PREC_EQUALITY},
    [TOKEN_BANG_EQ] = {NULL, binary, PREC_EQUALITY},
    [TOKEN_LESS] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_LESS_EQ] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_GREATER] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_GREATER_EQ] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_AMP] = {NULL, binary, PREC_BITAND},
    [TOKEN_PIPE] = {NULL, binary, PREC_BITOR},
    [TOKEN_CARET] = {NULL, binary, PREC_BITXOR},
    [TOKEN_SHL] = {NULL, binary, PREC_SHIFT},
    [TOKEN_SHR] = {NULL, binary, PREC_SHIFT},
    [TOKEN_AMP_AMP] = {NULL, logical, PREC_AND},
    [TOKEN_PIPE_PIPE] = {NULL, logical, PREC_OR},
    [TOKEN_IDENTIFIER] = {variable, NULL, PREC_NONE},
    [TOKEN_STRING] = {string, NULL, PREC_NONE},
    [TOKEN_INT] = {number, NULL, PREC_NONE},
    [TOKEN_FLOAT] = {number, NULL, PREC_NONE},
    [TOKEN_TRUE] = {literal, NULL, PREC_NONE},
    [TOKEN_FALSE] = {literal, NULL, PREC_NONE},
    [TOKEN_NIL] = {literal, NULL, PREC_NONE},
    [TOKEN_EOF] = {NULL, NULL, PREC_NONE},
};

static const ParseRule *get_rule(TokenType type) { return &rules[type]; }

static Expr *parse_precedence(Precedence precedence) {
  advance();
  PrefixFn prefixRule = get_rule(parser.previous.type)->prefix;
  if (prefixRule == NULL) {
    error("Expect expression.");
    return make_literal(NIL_VAL, parser.previous.line);
  }

  bool canAssign = precedence <= PREC_ASSIGNMENT;
  Expr *left = prefixRule(canAssign);

  while (precedence <= get_rule(parser.current.type)->precedence) {
    advance();
    InfixFn infixRule = get_rule(parser.previous.type)->infix;
    left = infixRule(canAssign, left);
  }

  if (canAssign && match(TOKEN_EQ)) {
    error("Invalid assignment target.");
  }
  return left;
}

static Expr *expression(void) { return parse_precedence(PREC_ASSIGNMENT); }

/* ---- statements ------------------------------------------------------- */

static Stmt *declaration(void);
static Stmt *statement(void);
static Stmt *block(void);

static void synchronize(void) {
  parser.panicMode = false;
  while (parser.current.type != TOKEN_EOF) {
    if (parser.previous.type == TOKEN_SEMICOLON) return;
    switch (parser.current.type) {
      case TOKEN_TYPE:
      case TOKEN_IF:
      case TOKEN_WHILE:
      case TOKEN_FOR:
      case TOKEN_DO:
      case TOKEN_RETURN:
      case TOKEN_SWITCH:
      case TOKEN_LBRACE:
        return;
      default:;
    }
    advance();
  }
}

static Stmt *expr_statement(void) {
  int line = parser.current.line;
  Expr *e = expression();
  consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
  Stmt *s = stmt_new(STMT_EXPR, line);
  s->as.expr.expr = e;
  return s;
}

static Stmt *var_declaration(Token nameTok) {
  int line = nameTok.line;
  ObjString *name = intern_token(nameTok);
  Expr *init = NULL;
  if (match(TOKEN_EQ)) init = expression();
  consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");
  Stmt *s = stmt_new(STMT_VAR, line);
  s->as.var.name = name;
  s->as.var.init = init;
  return s;
}

static Stmt *function_declaration(Token nameTok) {
  int line = nameTok.line;
  ObjString *name = intern_token(nameTok);
  Stmt *s = stmt_new(STMT_FUNCTION, line);
  s->as.func.name = name;
  namelist_init(&s->as.func.params);

  consume(TOKEN_LPAREN, "Expect '(' after function name.");
  if (!check(TOKEN_RPAREN)) {
    do {
      consume(TOKEN_TYPE, "Expect parameter type.");
      if (match(TOKEN_LBRACKET)) {
        consume(TOKEN_RBRACKET, "Expect ']' in array type.");
      }
      consume(TOKEN_IDENTIFIER, "Expect parameter name.");
      namelist_write(&s->as.func.params, intern_token(parser.previous));
    } while (match(TOKEN_COMMA));
  }
  consume(TOKEN_RPAREN, "Expect ')' after parameters.");
  consume(TOKEN_LBRACE, "Expect '{' before function body.");
  s->as.func.body = block();
  return s;
}

/* A declaration begins with a type keyword; `<type> name (` is a function,
 * otherwise it is a variable. */
static Stmt *typed_declaration(void) {
  if (match(TOKEN_LBRACKET)) consume(TOKEN_RBRACKET, "Expect ']' in array type.");
  consume(TOKEN_IDENTIFIER, "Expect name after type.");
  Token nameTok = parser.previous;
  if (check(TOKEN_LPAREN)) return function_declaration(nameTok);
  return var_declaration(nameTok);
}

static Stmt *block(void) {
  int line = parser.previous.line;
  Stmt *s = stmt_new(STMT_BLOCK, line);
  stmtlist_init(&s->as.block.list);
  while (!check(TOKEN_RBRACE) && !check(TOKEN_EOF)) {
    stmtlist_write(&s->as.block.list, declaration());
  }
  consume(TOKEN_RBRACE, "Expect '}' after block.");
  return s;
}

static Stmt *if_statement(void) {
  int line = parser.previous.line;
  consume(TOKEN_LPAREN, "Expect '(' after 'if'.");
  Expr *cond = expression();
  consume(TOKEN_RPAREN, "Expect ')' after condition.");
  Stmt *s = stmt_new(STMT_IF, line);
  s->as.iff.cond = cond;
  s->as.iff.thenB = statement();
  s->as.iff.elseB = match(TOKEN_ELSE) ? statement() : NULL;
  return s;
}

static Stmt *while_statement(void) {
  int line = parser.previous.line;
  consume(TOKEN_LPAREN, "Expect '(' after 'while'.");
  Expr *cond = expression();
  consume(TOKEN_RPAREN, "Expect ')' after condition.");
  Stmt *s = stmt_new(STMT_WHILE, line);
  s->as.whilef.cond = cond;
  s->as.whilef.body = statement();
  return s;
}

static Stmt *do_while_statement(void) {
  int line = parser.previous.line;
  Stmt *s = stmt_new(STMT_DOWHILE, line);
  s->as.dowhile.body = statement();
  consume(TOKEN_WHILE, "Expect 'while' after do body.");
  consume(TOKEN_LPAREN, "Expect '(' after 'while'.");
  s->as.dowhile.cond = expression();
  consume(TOKEN_RPAREN, "Expect ')' after condition.");
  consume(TOKEN_SEMICOLON, "Expect ';' after do-while.");
  return s;
}

static Stmt *for_statement(void) {
  int line = parser.previous.line;
  consume(TOKEN_LPAREN, "Expect '(' after 'for'.");
  Stmt *s = stmt_new(STMT_FOR, line);

  if (match(TOKEN_SEMICOLON)) {
    s->as.forf.init = NULL;
  } else if (match(TOKEN_TYPE)) {
    s->as.forf.init = typed_declaration();
  } else {
    s->as.forf.init = expr_statement();
  }

  s->as.forf.cond = NULL;
  if (!check(TOKEN_SEMICOLON)) s->as.forf.cond = expression();
  consume(TOKEN_SEMICOLON, "Expect ';' after loop condition.");

  s->as.forf.incr = NULL;
  if (!check(TOKEN_RPAREN)) s->as.forf.incr = expression();
  consume(TOKEN_RPAREN, "Expect ')' after for clauses.");

  s->as.forf.body = statement();
  return s;
}

static Stmt *return_statement(void) {
  int line = parser.previous.line;
  Stmt *s = stmt_new(STMT_RETURN, line);
  s->as.ret.value = NULL;
  if (!check(TOKEN_SEMICOLON)) s->as.ret.value = expression();
  consume(TOKEN_SEMICOLON, "Expect ';' after return value.");
  return s;
}

static Stmt *switch_statement(void) {
  int line = parser.previous.line;
  consume(TOKEN_LPAREN, "Expect '(' after 'switch'.");
  Stmt *s = stmt_new(STMT_SWITCH, line);
  s->as.switchf.disc = expression();
  consume(TOKEN_RPAREN, "Expect ')' after switch value.");
  consume(TOKEN_LBRACE, "Expect '{' before switch body.");
  caselist_init(&s->as.switchf.cases);

  while (!check(TOKEN_RBRACE) && !check(TOKEN_EOF)) {
    SwitchCase c;
    c.value = NULL;
    stmtlist_init(&c.body);
    if (match(TOKEN_CASE)) {
      c.value = expression();
    } else {
      consume(TOKEN_DEFAULT, "Expect 'case' or 'default'.");
    }
    consume(TOKEN_COLON, "Expect ':' after case label.");
    while (!check(TOKEN_CASE) && !check(TOKEN_DEFAULT) &&
           !check(TOKEN_RBRACE) && !check(TOKEN_EOF)) {
      stmtlist_write(&c.body, declaration());
    }
    caselist_write(&s->as.switchf.cases, c);
  }
  consume(TOKEN_RBRACE, "Expect '}' after switch body.");
  return s;
}

static Stmt *simple_statement(StmtKind kind) {
  int line = parser.previous.line;
  consume(TOKEN_SEMICOLON, "Expect ';' after statement.");
  return stmt_new(kind, line);
}

/* HolyC printing: a bare string statement is printed, optionally with
 * printf-style trailing arguments: `"x = %d\n", x;` */
static Stmt *print_statement(void) {
  int line = parser.current.line;
  Stmt *s = stmt_new(STMT_PRINT, line);
  exprlist_init(&s->as.print.args);
  advance(); /* consume the string token */
  exprlist_write(&s->as.print.args, string(false));
  while (match(TOKEN_COMMA)) {
    exprlist_write(&s->as.print.args, expression());
  }
  consume(TOKEN_SEMICOLON, "Expect ';' after print statement.");
  return s;
}

static Stmt *statement(void) {
  if (match(TOKEN_LBRACE)) return block();
  if (match(TOKEN_IF)) return if_statement();
  if (match(TOKEN_WHILE)) return while_statement();
  if (match(TOKEN_FOR)) return for_statement();
  if (match(TOKEN_DO)) return do_while_statement();
  if (match(TOKEN_RETURN)) return return_statement();
  if (match(TOKEN_SWITCH)) return switch_statement();
  if (match(TOKEN_BREAK)) return simple_statement(STMT_BREAK);
  if (match(TOKEN_CONTINUE)) return simple_statement(STMT_CONTINUE);
  if (check(TOKEN_STRING)) return print_statement();
  return expr_statement();
}

static Stmt *declaration(void) {
  Stmt *s;
  if (match(TOKEN_TYPE)) {
    s = typed_declaration();
  } else {
    s = statement();
  }
  if (parser.panicMode) synchronize();
  return s;
}

bool parse(const char *source, StmtList *out) {
  lexer_init(source);
  parser.hadError = false;
  parser.panicMode = false;
  stmtlist_init(out);
  advance();
  while (!check(TOKEN_EOF)) {
    stmtlist_write(out, declaration());
  }
  return !parser.hadError;
}
