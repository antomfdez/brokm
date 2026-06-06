/* typecheck.c - static type-checking pass (see typecheck.h).
 *
 * A single walk over the AST with three side tables: a flat scope stack of
 * variable bindings, a function-signature registry (so calls check arity and
 * argument types, with forward references resolved by a pre-pass), and a class
 * registry (so field access is checked against declared fields). Built-in
 * natives are registered as variadic functions returning TY_UNKNOWN so calls to
 * them are never flagged. Errors are reported but do not stop the walk, so one
 * run surfaces as many problems as possible. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "typecheck.h"

/* ---- state ------------------------------------------------------------ */

typedef struct {
  ObjString *name;
  Type type;
  int depth;
} TcVar;

typedef struct {
  ObjString *name;
  Type ret;
  Type *params; /* borrowed from the AST; not owned */
  int arity;
  bool variadic; /* natives: skip arity/argument checks */
} TcFunc;

typedef struct {
  ObjString *name;
  Stmt *decl; /* the STMT_CLASS node, for field lookups */
} TcClass;

typedef struct {
  TcVar *vars;
  int varCount, varCap;
  int depth;

  TcFunc *funcs;
  int funcCount, funcCap;

  TcClass *classes;
  int classCount, classCap;

  Type currentReturn; /* return type of the function being checked */
  bool hadError;
} Checker;

static Checker tc;

/* ---- error reporting -------------------------------------------------- */

static void tc_error(int line, const char *format, ...) {
  va_list args;
  va_start(args, format);
  fprintf(stderr, "[line %d] Type error: ", line);
  vfprintf(stderr, format, args);
  va_end(args);
  fputc('\n', stderr);
  tc.hadError = true;
}

/* ---- scopes ----------------------------------------------------------- */

static void begin_scope(void) { tc.depth++; }

static void end_scope(void) {
  tc.depth--;
  while (tc.varCount > 0 && tc.vars[tc.varCount - 1].depth > tc.depth) {
    tc.varCount--;
  }
}

static void declare_var(ObjString *name, Type type) {
  if (tc.varCap < tc.varCount + 1) {
    int old = tc.varCap;
    tc.varCap = GROW_CAPACITY(old);
    tc.vars = GROW_ARRAY(TcVar, tc.vars, old, tc.varCap);
  }
  tc.vars[tc.varCount].name = name;
  tc.vars[tc.varCount].type = type;
  tc.vars[tc.varCount].depth = tc.depth;
  tc.varCount++;
}

/* Resolve a variable's type. Returns false if the name is not in scope, in
 * which case the caller treats it as TY_UNKNOWN (gradual typing). */
static bool resolve_var(ObjString *name, Type *out) {
  for (int i = tc.varCount - 1; i >= 0; i--) {
    if (tc.vars[i].name == name) { /* interned: pointer equality */
      *out = tc.vars[i].type;
      return true;
    }
  }
  return false;
}

/* ---- function and class registries ------------------------------------ */

static TcFunc *find_func(ObjString *name) {
  for (int i = 0; i < tc.funcCount; i++) {
    if (tc.funcs[i].name == name) return &tc.funcs[i];
  }
  return NULL;
}

static void register_func(ObjString *name, Type ret, Type *params, int arity,
                          bool variadic) {
  if (find_func(name) != NULL) return; /* first declaration wins */
  if (tc.funcCap < tc.funcCount + 1) {
    int old = tc.funcCap;
    tc.funcCap = GROW_CAPACITY(old);
    tc.funcs = GROW_ARRAY(TcFunc, tc.funcs, old, tc.funcCap);
  }
  tc.funcs[tc.funcCount].name = name;
  tc.funcs[tc.funcCount].ret = ret;
  tc.funcs[tc.funcCount].params = params;
  tc.funcs[tc.funcCount].arity = arity;
  tc.funcs[tc.funcCount].variadic = variadic;
  tc.funcCount++;
}

static TcClass *find_class(ObjString *name) {
  for (int i = 0; i < tc.classCount; i++) {
    if (tc.classes[i].name == name) return &tc.classes[i];
  }
  return NULL;
}

static void register_class(Stmt *decl) {
  if (find_class(decl->as.klass.name) != NULL) return;
  if (tc.classCap < tc.classCount + 1) {
    int old = tc.classCap;
    tc.classCap = GROW_CAPACITY(old);
    tc.classes = GROW_ARRAY(TcClass, tc.classes, old, tc.classCap);
  }
  tc.classes[tc.classCount].name = decl->as.klass.name;
  tc.classes[tc.classCount].decl = decl;
  tc.classCount++;
}

/* Find a field's declared type on a class. Returns false if no such field. */
static bool class_field_type(TcClass *cls, ObjString *field, Type *out) {
  NameList *fields = &cls->decl->as.klass.fields;
  Type *types = cls->decl->as.klass.fieldTypes;
  for (int i = 0; i < fields->count; i++) {
    if (fields->items[i] == field) {
      *out = types[i];
      return true;
    }
  }
  return false;
}

static void register_natives(void) {
  /* Names from natives_register() in natives.c. All variadic, TY_UNKNOWN
   * result, so calls to them are never flagged. */
  static const char *NAMES[] = {
      "Print",  "PrintErr",  "GcCollect", "GcMinor", "GcDisable", "GcEnable",
      "Len",    "Append",    "MAlloc",    "Free",    "PeekU8",
      "PokeU8", "PeekI64",   "PokeI64",   "PeekF64", "PokeF64",
      "PeekPtr", "PokePtr",
      /* v0.6 standard library */
      "CharAt", "Chr",       "Substr",    "IndexOf", "ToInt",     "ToStr",
      "ReadFile", "WriteFile",
      "Abs",    "Min",       "Max",       "Sqrt",    "Pow",       "Floor", "Ceil",
      /* v0.7 maps */
      "MapNew", "MapSet",    "MapGet",    "MapHas",  "MapDelete", "MapLen", "MapKeys",
  };
  size_t n = sizeof(NAMES) / sizeof(NAMES[0]);
  for (size_t i = 0; i < n; i++) {
    ObjString *name = string_copy(NAMES[i], (int)strlen(NAMES[i]));
    register_func(name, ty(TY_UNKNOWN), NULL, 0, true);
  }
}

/* ---- operator names for diagnostics ----------------------------------- */

static const char *op_lexeme(TokenType op) {
  switch (op) {
    case TOKEN_PLUS: return "+";
    case TOKEN_MINUS: return "-";
    case TOKEN_STAR: return "*";
    case TOKEN_SLASH: return "/";
    case TOKEN_PERCENT: return "%";
    case TOKEN_AMP: return "&";
    case TOKEN_PIPE: return "|";
    case TOKEN_CARET: return "^";
    case TOKEN_SHL: return "<<";
    case TOKEN_SHR: return ">>";
    case TOKEN_LESS: return "<";
    case TOKEN_LESS_EQ: return "<=";
    case TOKEN_GREATER: return ">";
    case TOKEN_GREATER_EQ: return ">=";
    case TOKEN_TILDE: return "~";
    default: return "?";
  }
}

/* ---- expressions ------------------------------------------------------ */

static Type check_expr(Expr *expr);

static Type literal_type(Value v) {
  switch (v.type) {
    case VAL_BOOL: return ty(TY_BOOL);
    case VAL_INT: return ty(TY_INT);
    case VAL_FLOAT: return ty(TY_FLOAT);
    case VAL_NIL: return ty(TY_NIL);
    case VAL_PTR: return ty(TY_PTR);
    case VAL_OBJ: return IS_STRING(v) ? ty(TY_STRING) : ty(TY_UNKNOWN);
    default: return ty(TY_UNKNOWN);
  }
}

static Type check_binary(Expr *expr) {
  TokenType op = expr->as.binary.op;
  Type l = check_expr(expr->as.binary.left);
  Type r = check_expr(expr->as.binary.right);
  int line = expr->line;

  switch (op) {
    case TOKEN_PLUS: {
      /* '+' is numeric addition or string concatenation; only aggregate
       * operands (instances, classes, arrays) are invalid. */
      bool lbad = l.kind == TY_INSTANCE || l.kind == TY_CLASS ||
                  l.kind == TY_ARRAY;
      bool rbad = r.kind == TY_INSTANCE || r.kind == TY_CLASS ||
                  r.kind == TY_ARRAY;
      if (lbad || rbad) {
        tc_error(line,
                 "operands of '+' must be numbers or strings (got %s and %s)",
                 type_name(l), type_name(r));
        return ty(TY_UNKNOWN);
      }
      if (l.kind == TY_STRING && r.kind == TY_STRING) return ty(TY_STRING);
      if (type_is_numeric(l) && type_is_numeric(r)) {
        return (l.kind == TY_FLOAT || r.kind == TY_FLOAT) ? ty(TY_FLOAT)
                                                          : ty(TY_INT);
      }
      return ty(TY_UNKNOWN); /* mixed scalar/string/unknown: not pinned down */
    }
    case TOKEN_MINUS:
    case TOKEN_STAR:
    case TOKEN_SLASH:
    case TOKEN_PERCENT: {
      bool lok = type_is_numeric(l) || l.kind == TY_UNKNOWN;
      bool rok = type_is_numeric(r) || r.kind == TY_UNKNOWN;
      if (!lok || !rok) {
        tc_error(line, "operands of '%s' must be numeric (got %s and %s)",
                 op_lexeme(op), type_name(l), type_name(r));
        return ty(TY_UNKNOWN);
      }
      if (l.kind == TY_UNKNOWN || r.kind == TY_UNKNOWN) return ty(TY_UNKNOWN);
      return (l.kind == TY_FLOAT || r.kind == TY_FLOAT) ? ty(TY_FLOAT)
                                                        : ty(TY_INT);
    }
    case TOKEN_AMP:
    case TOKEN_PIPE:
    case TOKEN_CARET:
    case TOKEN_SHL:
    case TOKEN_SHR: {
      bool lok = l.kind == TY_INT || l.kind == TY_UNKNOWN;
      bool rok = r.kind == TY_INT || r.kind == TY_UNKNOWN;
      if (!lok || !rok) {
        tc_error(line, "operands of '%s' must be integers (got %s and %s)",
                 op_lexeme(op), type_name(l), type_name(r));
      }
      return ty(TY_INT);
    }
    case TOKEN_LESS:
    case TOKEN_LESS_EQ:
    case TOKEN_GREATER:
    case TOKEN_GREATER_EQ: {
      bool lok = type_is_numeric(l) || l.kind == TY_UNKNOWN;
      bool rok = type_is_numeric(r) || r.kind == TY_UNKNOWN;
      if (!lok || !rok) {
        tc_error(line, "operands of '%s' must be numeric (got %s and %s)",
                 op_lexeme(op), type_name(l), type_name(r));
      }
      return ty(TY_BOOL);
    }
    case TOKEN_EQ_EQ:
    case TOKEN_BANG_EQ:
      return ty(TY_BOOL); /* any two values may be compared for equality */
    default:
      return ty(TY_UNKNOWN);
  }
}

static Type check_unary(Expr *expr) {
  Type t = check_expr(expr->as.unary.operand);
  switch (expr->as.unary.op) {
    case TOKEN_MINUS:
      if (!type_is_numeric(t) && t.kind != TY_UNKNOWN) {
        tc_error(expr->line, "operand of unary '-' must be numeric (got %s)",
                 type_name(t));
        return ty(TY_UNKNOWN);
      }
      return t;
    case TOKEN_TILDE:
      if (t.kind != TY_INT && t.kind != TY_UNKNOWN) {
        tc_error(expr->line, "operand of '~' must be an integer (got %s)",
                 type_name(t));
      }
      return ty(TY_INT);
    case TOKEN_BANG:
      return ty(TY_BOOL);
    default:
      return ty(TY_UNKNOWN);
  }
}

static Type check_call(Expr *expr) {
  Expr *callee = expr->as.call.callee;
  ExprList *args = &expr->as.call.args;

  /* Type every argument first (sets their .type and surfaces nested errors). */
  Type argTypes[256];
  int argc = args->count;
  for (int i = 0; i < argc; i++) {
    Type at = check_expr(args->items[i]);
    if (i < 256) argTypes[i] = at;
  }

  if (callee->kind == EXPR_VARIABLE) {
    ObjString *name = callee->as.variable.name;

    TcClass *cls = find_class(name);
    if (cls != NULL) {
      callee->type = ty_named(TY_CLASS, name);
      int fieldCount = cls->decl->as.klass.fields.count;
      if (argc > fieldCount) {
        tc_error(expr->line,
                 "too many arguments to %s constructor (expected at most %d, "
                 "got %d)",
                 name->chars, fieldCount, argc);
      } else {
        Type *ftypes = cls->decl->as.klass.fieldTypes;
        for (int i = 0; i < argc && i < 256; i++) {
          if (!type_assignable(ftypes[i], argTypes[i])) {
            tc_error(expr->line,
                     "field '%s' of %s expects %s but got %s",
                     cls->decl->as.klass.fields.items[i]->chars, name->chars,
                     type_name(ftypes[i]), type_name(argTypes[i]));
          }
        }
      }
      return ty_named(TY_INSTANCE, name);
    }

    TcFunc *fn = find_func(name);
    if (fn != NULL) {
      callee->type = ty(TY_FUNCTION);
      if (!fn->variadic) {
        if (argc != fn->arity) {
          tc_error(expr->line,
                   "%s expects %d argument%s but got %d", name->chars,
                   fn->arity, fn->arity == 1 ? "" : "s", argc);
        } else {
          for (int i = 0; i < argc && i < 256; i++) {
            if (!type_assignable(fn->params[i], argTypes[i])) {
              tc_error(expr->line,
                       "argument %d of %s expects %s but got %s", i + 1,
                       name->chars, type_name(fn->params[i]),
                       type_name(argTypes[i]));
            }
          }
        }
      }
      return fn->ret;
    }
  }

  /* Unknown or computed callee: check it, then give up on the result type. */
  check_expr(callee);
  return ty(TY_UNKNOWN);
}

static Type check_expr(Expr *expr) {
  Type t;
  switch (expr->kind) {
    case EXPR_LITERAL:
      t = literal_type(expr->as.literal.value);
      break;
    case EXPR_VARIABLE: {
      ObjString *name = expr->as.variable.name;
      if (!resolve_var(name, &t)) {
        if (find_func(name) != NULL) {
          t = ty(TY_FUNCTION);
        } else if (find_class(name) != NULL) {
          t = ty_named(TY_CLASS, name);
        } else {
          t = ty(TY_UNKNOWN);
        }
      }
      break;
    }
    case EXPR_ASSIGN: {
      Type vt = check_expr(expr->as.assign.value);
      Type target;
      if (resolve_var(expr->as.assign.name, &target)) {
        if (!type_assignable(target, vt)) {
          tc_error(expr->line, "cannot assign %s to '%s' of type %s",
                   type_name(vt), expr->as.assign.name->chars,
                   type_name(target));
        }
        t = target;
      } else {
        t = vt;
      }
      break;
    }
    case EXPR_UNARY:
      t = check_unary(expr);
      break;
    case EXPR_BINARY:
      t = check_binary(expr);
      break;
    case EXPR_LOGICAL:
      check_expr(expr->as.logical.left);
      check_expr(expr->as.logical.right);
      t = ty(TY_BOOL);
      break;
    case EXPR_CALL:
      t = check_call(expr);
      break;
    case EXPR_ARRAY:
      for (int i = 0; i < expr->as.array.elements.count; i++) {
        check_expr(expr->as.array.elements.items[i]);
      }
      t = ty(TY_ARRAY);
      break;
    case EXPR_INDEX: {
      Type ot = check_expr(expr->as.index.object);
      Type it = check_expr(expr->as.index.index);
      if (ot.kind != TY_ARRAY && ot.kind != TY_STRING && ot.kind != TY_PTR &&
          ot.kind != TY_UNKNOWN) {
        tc_error(expr->line, "cannot index a value of type %s", type_name(ot));
      }
      if (it.kind != TY_INT && it.kind != TY_UNKNOWN) {
        tc_error(expr->line, "array index must be an integer (got %s)",
                 type_name(it));
      }
      t = ty(TY_UNKNOWN); /* element type is not tracked */
      break;
    }
    case EXPR_INDEX_SET: {
      Type ot = check_expr(expr->as.index_set.object);
      Type it = check_expr(expr->as.index_set.index);
      t = check_expr(expr->as.index_set.value);
      if (ot.kind != TY_ARRAY && ot.kind != TY_PTR && ot.kind != TY_UNKNOWN) {
        tc_error(expr->line, "cannot index-assign a value of type %s",
                 type_name(ot));
      }
      if (it.kind != TY_INT && it.kind != TY_UNKNOWN) {
        tc_error(expr->line, "array index must be an integer (got %s)",
                 type_name(it));
      }
      break;
    }
    case EXPR_FIELD: {
      Type ot = check_expr(expr->as.field.object);
      t = ty(TY_UNKNOWN);
      if (ot.kind == TY_INSTANCE) {
        TcClass *cls = ot.name ? find_class(ot.name) : NULL;
        if (cls != NULL && !class_field_type(cls, expr->as.field.name, &t)) {
          tc_error(expr->line, "unknown field '%s' on %s",
                   expr->as.field.name->chars, ot.name->chars);
          t = ty(TY_UNKNOWN);
        }
      } else if (ot.kind != TY_UNKNOWN) {
        tc_error(expr->line, "cannot access field '%s' on a value of type %s",
                 expr->as.field.name->chars, type_name(ot));
      }
      break;
    }
    case EXPR_FIELD_SET: {
      Type ot = check_expr(expr->as.field_set.object);
      Type vt = check_expr(expr->as.field_set.value);
      t = vt;
      if (ot.kind == TY_INSTANCE) {
        TcClass *cls = ot.name ? find_class(ot.name) : NULL;
        Type ft;
        if (cls != NULL && !class_field_type(cls, expr->as.field_set.name, &ft)) {
          tc_error(expr->line, "unknown field '%s' on %s",
                   expr->as.field_set.name->chars, ot.name->chars);
        } else if (cls != NULL && !type_assignable(ft, vt)) {
          tc_error(expr->line, "field '%s' of %s expects %s but got %s",
                   expr->as.field_set.name->chars, ot.name->chars,
                   type_name(ft), type_name(vt));
        }
      } else if (ot.kind != TY_UNKNOWN) {
        tc_error(expr->line, "cannot set field '%s' on a value of type %s",
                 expr->as.field_set.name->chars, type_name(ot));
      }
      break;
    }
    default:
      t = ty(TY_UNKNOWN);
      break;
  }
  expr->type = t;
  return t;
}

/* ---- statements ------------------------------------------------------- */

static void check_stmt(Stmt *stmt);

static void check_stmtlist(StmtList *list) {
  for (int i = 0; i < list->count; i++) check_stmt(list->items[i]);
}

static void check_function(Stmt *stmt) {
  /* Nested functions are registered on encounter; top-level ones already are. */
  register_func(stmt->as.func.name, stmt->as.func.returnType,
                stmt->as.func.paramTypes, stmt->as.func.params.count, false);

  Type savedReturn = tc.currentReturn;
  tc.currentReturn = stmt->as.func.returnType;
  begin_scope();
  NameList *params = &stmt->as.func.params;
  for (int i = 0; i < params->count; i++) {
    declare_var(params->items[i], stmt->as.func.paramTypes[i]);
  }
  check_stmt(stmt->as.func.body);
  end_scope();
  tc.currentReturn = savedReturn;
}

static void check_stmt(Stmt *stmt) {
  switch (stmt->kind) {
    case STMT_EXPR:
      check_expr(stmt->as.expr.expr);
      break;
    case STMT_VAR: {
      if (stmt->as.var.init != NULL) {
        Type it = check_expr(stmt->as.var.init);
        Type declared = stmt->as.var.declared;
        if (!type_assignable(declared, it)) {
          tc_error(stmt->line, "cannot initialize '%s' of type %s with %s",
                   stmt->as.var.name->chars, type_name(declared),
                   type_name(it));
        }
      }
      declare_var(stmt->as.var.name, stmt->as.var.declared);
      break;
    }
    case STMT_BLOCK:
      begin_scope();
      check_stmtlist(&stmt->as.block.list);
      end_scope();
      break;
    case STMT_IF:
      check_expr(stmt->as.iff.cond);
      check_stmt(stmt->as.iff.thenB);
      if (stmt->as.iff.elseB) check_stmt(stmt->as.iff.elseB);
      break;
    case STMT_WHILE:
      check_expr(stmt->as.whilef.cond);
      check_stmt(stmt->as.whilef.body);
      break;
    case STMT_FOR:
      begin_scope();
      if (stmt->as.forf.init) check_stmt(stmt->as.forf.init);
      if (stmt->as.forf.cond) check_expr(stmt->as.forf.cond);
      if (stmt->as.forf.incr) check_expr(stmt->as.forf.incr);
      check_stmt(stmt->as.forf.body);
      end_scope();
      break;
    case STMT_DOWHILE:
      check_stmt(stmt->as.dowhile.body);
      check_expr(stmt->as.dowhile.cond);
      break;
    case STMT_RETURN:
      if (stmt->as.ret.value) {
        Type vt = check_expr(stmt->as.ret.value);
        if (tc.currentReturn.kind == TY_VOID) {
          tc_error(stmt->line, "returning a value from a U0 function");
        } else if (!type_assignable(tc.currentReturn, vt)) {
          tc_error(stmt->line, "returning %s from a function declared %s",
                   type_name(vt), type_name(tc.currentReturn));
        }
      }
      break;
    case STMT_PRINT:
      for (int i = 0; i < stmt->as.print.args.count; i++) {
        check_expr(stmt->as.print.args.items[i]);
      }
      break;
    case STMT_FUNCTION:
      check_function(stmt);
      break;
    case STMT_SWITCH:
      check_expr(stmt->as.switchf.disc);
      begin_scope();
      for (int i = 0; i < stmt->as.switchf.cases.count; i++) {
        if (stmt->as.switchf.cases.items[i].value) {
          check_expr(stmt->as.switchf.cases.items[i].value);
        }
        check_stmtlist(&stmt->as.switchf.cases.items[i].body);
      }
      end_scope();
      break;
    case STMT_BREAK:
    case STMT_CONTINUE:
      break;
    case STMT_CLASS:
      register_class(stmt); /* top-level already registered; no-op then */
      break;
  }
}

/* ---- entry point ------------------------------------------------------ */

bool typecheck(StmtList *program) {
  tc.vars = NULL;
  tc.varCount = tc.varCap = 0;
  tc.depth = 0;
  tc.funcs = NULL;
  tc.funcCount = tc.funcCap = 0;
  tc.classes = NULL;
  tc.classCount = tc.classCap = 0;
  tc.currentReturn = ty(TY_UNKNOWN);
  tc.hadError = false;

  register_natives();

  /* Pre-pass: register every top-level function and class so calls can resolve
   * forward references and constructors. */
  for (int i = 0; i < program->count; i++) {
    Stmt *s = program->items[i];
    if (s->kind == STMT_FUNCTION) {
      register_func(s->as.func.name, s->as.func.returnType,
                    s->as.func.paramTypes, s->as.func.params.count, false);
      declare_var(s->as.func.name, ty(TY_FUNCTION));
    } else if (s->kind == STMT_CLASS) {
      register_class(s);
      declare_var(s->as.klass.name, ty_named(TY_CLASS, s->as.klass.name));
    }
  }

  for (int i = 0; i < program->count; i++) check_stmt(program->items[i]);

  bool ok = !tc.hadError;
  FREE_ARRAY(TcVar, tc.vars, tc.varCap);
  FREE_ARRAY(TcFunc, tc.funcs, tc.funcCap);
  FREE_ARRAY(TcClass, tc.classes, tc.classCap);
  return ok;
}
