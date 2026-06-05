/* ast.c - AST node and list construction plus recursive teardown.
 *
 * Nodes are heap-allocated via reallocate() and freed once the bytecode
 * compiler has consumed the tree (the VM never sees the AST). */
#include "ast.h"
#include "memory.h"

Expr *expr_new(ExprKind kind, int line) {
  Expr *expr = ALLOCATE(Expr, 1);
  expr->kind = kind;
  expr->line = line;
  expr->type = ty(TY_UNKNOWN);
  return expr;
}

Stmt *stmt_new(StmtKind kind, int line) {
  Stmt *stmt = ALLOCATE(Stmt, 1);
  stmt->kind = kind;
  stmt->line = line;
  return stmt;
}

void exprlist_init(ExprList *list) {
  list->items = NULL;
  list->count = 0;
  list->capacity = 0;
}

void exprlist_write(ExprList *list, Expr *expr) {
  if (list->capacity < list->count + 1) {
    int oldCap = list->capacity;
    list->capacity = GROW_CAPACITY(oldCap);
    list->items = GROW_ARRAY(Expr *, list->items, oldCap, list->capacity);
  }
  list->items[list->count++] = expr;
}

void stmtlist_init(StmtList *list) {
  list->items = NULL;
  list->count = 0;
  list->capacity = 0;
}

void stmtlist_write(StmtList *list, Stmt *stmt) {
  if (list->capacity < list->count + 1) {
    int oldCap = list->capacity;
    list->capacity = GROW_CAPACITY(oldCap);
    list->items = GROW_ARRAY(Stmt *, list->items, oldCap, list->capacity);
  }
  list->items[list->count++] = stmt;
}

void namelist_init(NameList *list) {
  list->items = NULL;
  list->count = 0;
  list->capacity = 0;
}

void namelist_write(NameList *list, ObjString *name) {
  if (list->capacity < list->count + 1) {
    int oldCap = list->capacity;
    list->capacity = GROW_CAPACITY(oldCap);
    list->items = GROW_ARRAY(ObjString *, list->items, oldCap, list->capacity);
  }
  list->items[list->count++] = name;
}

void caselist_init(CaseList *list) {
  list->items = NULL;
  list->count = 0;
  list->capacity = 0;
}

void caselist_write(CaseList *list, SwitchCase c) {
  if (list->capacity < list->count + 1) {
    int oldCap = list->capacity;
    list->capacity = GROW_CAPACITY(oldCap);
    list->items = GROW_ARRAY(SwitchCase, list->items, oldCap, list->capacity);
  }
  list->items[list->count++] = c;
}

void free_expr(Expr *expr) {
  if (expr == NULL) return;
  switch (expr->kind) {
    case EXPR_LITERAL:
    case EXPR_VARIABLE:
      break;
    case EXPR_UNARY:
      free_expr(expr->as.unary.operand);
      break;
    case EXPR_BINARY:
      free_expr(expr->as.binary.left);
      free_expr(expr->as.binary.right);
      break;
    case EXPR_LOGICAL:
      free_expr(expr->as.logical.left);
      free_expr(expr->as.logical.right);
      break;
    case EXPR_ASSIGN:
      free_expr(expr->as.assign.value);
      break;
    case EXPR_CALL:
      free_expr(expr->as.call.callee);
      for (int i = 0; i < expr->as.call.args.count; i++) {
        free_expr(expr->as.call.args.items[i]);
      }
      FREE_ARRAY(Expr *, expr->as.call.args.items, expr->as.call.args.capacity);
      break;
    case EXPR_ARRAY:
      for (int i = 0; i < expr->as.array.elements.count; i++) {
        free_expr(expr->as.array.elements.items[i]);
      }
      FREE_ARRAY(Expr *, expr->as.array.elements.items,
                 expr->as.array.elements.capacity);
      break;
    case EXPR_INDEX:
      free_expr(expr->as.index.object);
      free_expr(expr->as.index.index);
      break;
    case EXPR_INDEX_SET:
      free_expr(expr->as.index_set.object);
      free_expr(expr->as.index_set.index);
      free_expr(expr->as.index_set.value);
      break;
    case EXPR_FIELD:
      free_expr(expr->as.field.object);
      break;
    case EXPR_FIELD_SET:
      free_expr(expr->as.field_set.object);
      free_expr(expr->as.field_set.value);
      break;
  }
  FREE(Expr, expr);
}

void free_stmtlist(StmtList *list) {
  for (int i = 0; i < list->count; i++) free_stmt(list->items[i]);
  FREE_ARRAY(Stmt *, list->items, list->capacity);
  stmtlist_init(list);
}

void free_stmt(Stmt *stmt) {
  if (stmt == NULL) return;
  switch (stmt->kind) {
    case STMT_EXPR:
      free_expr(stmt->as.expr.expr);
      break;
    case STMT_VAR:
      free_expr(stmt->as.var.init);
      break;
    case STMT_BLOCK:
      free_stmtlist(&stmt->as.block.list);
      break;
    case STMT_IF:
      free_expr(stmt->as.iff.cond);
      free_stmt(stmt->as.iff.thenB);
      free_stmt(stmt->as.iff.elseB);
      break;
    case STMT_WHILE:
      free_expr(stmt->as.whilef.cond);
      free_stmt(stmt->as.whilef.body);
      break;
    case STMT_FOR:
      free_stmt(stmt->as.forf.init);
      free_expr(stmt->as.forf.cond);
      free_expr(stmt->as.forf.incr);
      free_stmt(stmt->as.forf.body);
      break;
    case STMT_DOWHILE:
      free_stmt(stmt->as.dowhile.body);
      free_expr(stmt->as.dowhile.cond);
      break;
    case STMT_RETURN:
      free_expr(stmt->as.ret.value);
      break;
    case STMT_PRINT:
      for (int i = 0; i < stmt->as.print.args.count; i++) {
        free_expr(stmt->as.print.args.items[i]);
      }
      FREE_ARRAY(Expr *, stmt->as.print.args.items,
                 stmt->as.print.args.capacity);
      break;
    case STMT_FUNCTION:
      FREE_ARRAY(Type, stmt->as.func.paramTypes, stmt->as.func.params.count);
      FREE_ARRAY(ObjString *, stmt->as.func.params.items,
                 stmt->as.func.params.capacity);
      free_stmt(stmt->as.func.body);
      break;
    case STMT_SWITCH:
      free_expr(stmt->as.switchf.disc);
      for (int i = 0; i < stmt->as.switchf.cases.count; i++) {
        free_expr(stmt->as.switchf.cases.items[i].value);
        free_stmtlist(&stmt->as.switchf.cases.items[i].body);
      }
      FREE_ARRAY(SwitchCase, stmt->as.switchf.cases.items,
                 stmt->as.switchf.cases.capacity);
      break;
    case STMT_CLASS:
      FREE_ARRAY(Type, stmt->as.klass.fieldTypes, stmt->as.klass.fields.count);
      FREE_ARRAY(ObjString *, stmt->as.klass.fields.items,
                 stmt->as.klass.fields.capacity);
      break;
    case STMT_BREAK:
    case STMT_CONTINUE:
      break;
  }
  FREE(Stmt, stmt);
}
