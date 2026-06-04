/* ast.h - the abstract syntax tree.
 *
 * The AST is the deliberate seam between the parser and the bytecode compiler.
 * The v0.4 type checker and v0.5 JIT will both operate over this tree, which is
 * why parsing and code generation are kept as separate passes in v0.1. */
#ifndef BROKM_AST_H
#define BROKM_AST_H

#include "common.h"
#include "token.h"
#include "value.h"

typedef struct Expr Expr;
typedef struct Stmt Stmt;

typedef struct {
  Expr **items;
  int count;
  int capacity;
} ExprList;

typedef struct {
  Stmt **items;
  int count;
  int capacity;
} StmtList;

typedef struct {
  ObjString **items;
  int count;
  int capacity;
} NameList;

typedef enum {
  EXPR_LITERAL,
  EXPR_VARIABLE,
  EXPR_UNARY,
  EXPR_BINARY,
  EXPR_LOGICAL,
  EXPR_ASSIGN,
  EXPR_CALL,
  EXPR_ARRAY,      /* [a, b, c] */
  EXPR_INDEX,      /* obj[index] */
  EXPR_INDEX_SET   /* obj[index] = value */
} ExprKind;

struct Expr {
  ExprKind kind;
  int line;
  union {
    struct { Value value; } literal;
    struct { ObjString *name; } variable;
    struct { TokenType op; Expr *operand; } unary;
    struct { TokenType op; Expr *left; Expr *right; } binary;
    struct { TokenType op; Expr *left; Expr *right; } logical;
    struct { ObjString *name; Expr *value; } assign;
    struct { Expr *callee; ExprList args; } call;
    struct { ExprList elements; } array;
    struct { Expr *object; Expr *index; } index;
    struct { Expr *object; Expr *index; Expr *value; } index_set;
  } as;
};

typedef enum {
  STMT_EXPR,
  STMT_VAR,
  STMT_BLOCK,
  STMT_IF,
  STMT_WHILE,
  STMT_FOR,
  STMT_DOWHILE,
  STMT_RETURN,
  STMT_PRINT,
  STMT_FUNCTION,
  STMT_BREAK,
  STMT_CONTINUE,
  STMT_SWITCH
} StmtKind;

typedef struct {
  Expr *value; /* NULL => default case */
  StmtList body;
} SwitchCase;

typedef struct {
  SwitchCase *items;
  int count;
  int capacity;
} CaseList;

struct Stmt {
  StmtKind kind;
  int line;
  union {
    struct { Expr *expr; } expr;
    struct { ObjString *name; Expr *init; } var;
    struct { StmtList list; } block;
    struct { Expr *cond; Stmt *thenB; Stmt *elseB; } iff;
    struct { Expr *cond; Stmt *body; } whilef;
    struct { Stmt *init; Expr *cond; Expr *incr; Stmt *body; } forf;
    struct { Stmt *body; Expr *cond; } dowhile;
    struct { Expr *value; } ret;
    struct { ExprList args; } print;
    struct { ObjString *name; NameList params; Stmt *body; } func;
    struct { Expr *disc; CaseList cases; } switchf;
  } as;
};

Expr *expr_new(ExprKind kind, int line);
Stmt *stmt_new(StmtKind kind, int line);

void exprlist_init(ExprList *list);
void exprlist_write(ExprList *list, Expr *expr);
void stmtlist_init(StmtList *list);
void stmtlist_write(StmtList *list, Stmt *stmt);
void namelist_init(NameList *list);
void namelist_write(NameList *list, ObjString *name);
void caselist_init(CaseList *list);
void caselist_write(CaseList *list, SwitchCase c);

void free_expr(Expr *expr);
void free_stmt(Stmt *stmt);
void free_stmtlist(StmtList *list);

#endif /* BROKM_AST_H */
