/* compiler.c - single-pass AST walk emitting stack-VM bytecode. */
#include <stdio.h>

#include "compiler.h"
#include "memory.h"

#ifdef BK_DEBUG_PRINT_CODE
#include "debug.h"
#endif

#define MAX_LOCALS BK_SLOT_COUNT
#define MAX_BREAKS 256

typedef struct {
  ObjString *name; /* NULL for synthetic slots (callee, switch value) */
  int depth;
} Local;

typedef struct Loop {
  struct Loop *enclosing;
  int continueTarget; /* bytecode offset to OP_LOOP back to */
  int localBase;      /* local count when the loop body began */
  bool isSwitch;      /* switch traps break but not continue */
  int breaks[MAX_BREAKS];
  int breakCount;
} Loop;

typedef struct Compiler {
  struct Compiler *enclosing;
  ObjFunction *function;
  Local locals[MAX_LOCALS];
  int localCount;
  int scopeDepth;
  Loop *loop;
} Compiler;

static Compiler *current = NULL;
static bool hadError = false;
static int currentLine = 0;

static void compile_error(const char *message) {
  fprintf(stderr, "[line %d] Compile error: %s\n", currentLine, message);
  hadError = true;
}

static Chunk *current_chunk(void) { return &current->function->chunk; }

/* ---- emit helpers ----------------------------------------------------- */

static void emit_byte(U8 byte) { chunk_write(current_chunk(), byte, currentLine); }

static void emit_bytes(U8 a, U8 b) {
  emit_byte(a);
  emit_byte(b);
}

static int make_constant(Value value) {
  int constant = chunk_add_constant(current_chunk(), value);
  if (constant > 255) {
    compile_error("Too many constants in one chunk.");
    return 0;
  }
  return constant;
}

static void emit_constant(Value value) {
  emit_bytes(OP_CONSTANT, (U8)make_constant(value));
}

static U8 identifier_constant(ObjString *name) {
  return (U8)make_constant(OBJ_VAL(name));
}

static int emit_jump(U8 op) {
  emit_byte(op);
  emit_byte(0xff);
  emit_byte(0xff);
  return current_chunk()->count - 2;
}

static void patch_jump(int offset) {
  int jump = current_chunk()->count - offset - 2;
  if (jump > 0xffff) compile_error("Jump distance too large.");
  current_chunk()->code[offset] = (U8)((jump >> 8) & 0xff);
  current_chunk()->code[offset + 1] = (U8)(jump & 0xff);
}

static void emit_loop(int loopStart) {
  emit_byte(OP_LOOP);
  int offset = current_chunk()->count - loopStart + 2;
  if (offset > 0xffff) compile_error("Loop body too large.");
  emit_byte((U8)((offset >> 8) & 0xff));
  emit_byte((U8)(offset & 0xff));
}

static void emit_return(void) {
  emit_byte(OP_NIL);
  emit_byte(OP_RETURN);
}

/* ---- scope and locals ------------------------------------------------- */

static void init_compiler(Compiler *compiler, ObjFunction *function,
                          Compiler *enclosing) {
  compiler->enclosing = enclosing;
  compiler->function = function;
  compiler->localCount = 0;
  compiler->scopeDepth = 0;
  compiler->loop = NULL;
  current = compiler;

  /* Reserve slot 0 for the executing function itself. */
  Local *slot0 = &compiler->locals[compiler->localCount++];
  slot0->name = NULL;
  slot0->depth = 0;
}

static void begin_scope(void) { current->scopeDepth++; }

static void end_scope(void) {
  current->scopeDepth--;
  while (current->localCount > 0 &&
         current->locals[current->localCount - 1].depth > current->scopeDepth) {
    emit_byte(OP_POP);
    current->localCount--;
  }
}

static void add_local(ObjString *name) {
  if (current->localCount >= MAX_LOCALS) {
    compile_error("Too many local variables in scope.");
    return;
  }
  Local *local = &current->locals[current->localCount++];
  local->name = name;
  local->depth = current->scopeDepth;
}

static int resolve_local(Compiler *compiler, ObjString *name) {
  for (int i = compiler->localCount - 1; i >= 0; i--) {
    Local *local = &compiler->locals[i];
    if (local->name == name) return i; /* interned: pointer equality */
  }
  return -1;
}

/* ---- loop / break / continue ------------------------------------------ */

static void begin_loop(Loop *loop, int continueTarget, bool isSwitch) {
  loop->enclosing = current->loop;
  loop->continueTarget = continueTarget;
  loop->localBase = current->localCount;
  loop->isSwitch = isSwitch;
  loop->breakCount = 0;
  current->loop = loop;
}

static void end_loop(void) {
  Loop *loop = current->loop;
  for (int i = 0; i < loop->breakCount; i++) patch_jump(loop->breaks[i]);
  current->loop = loop->enclosing;
}

static void pop_to(int targetLocalCount) {
  for (int i = current->localCount; i > targetLocalCount; i--) emit_byte(OP_POP);
}

/* ---- forward declarations --------------------------------------------- */

static void compile_expr(Expr *expr);
static void compile_stmt(Stmt *stmt);

/* ---- expressions ------------------------------------------------------ */

static U8 binary_op(TokenType op) {
  switch (op) {
    case TOKEN_PLUS: return OP_ADD;
    case TOKEN_MINUS: return OP_SUB;
    case TOKEN_STAR: return OP_MUL;
    case TOKEN_SLASH: return OP_DIV;
    case TOKEN_PERCENT: return OP_MOD;
    case TOKEN_EQ_EQ: return OP_EQUAL;
    case TOKEN_BANG_EQ: return OP_NOT_EQUAL;
    case TOKEN_LESS: return OP_LESS;
    case TOKEN_LESS_EQ: return OP_LESS_EQUAL;
    case TOKEN_GREATER: return OP_GREATER;
    case TOKEN_GREATER_EQ: return OP_GREATER_EQUAL;
    case TOKEN_AMP: return OP_BIT_AND;
    case TOKEN_PIPE: return OP_BIT_OR;
    case TOKEN_CARET: return OP_BIT_XOR;
    case TOKEN_SHL: return OP_SHL;
    case TOKEN_SHR: return OP_SHR;
    default: return OP_NIL; /* unreachable */
  }
}

static void compile_literal(Expr *expr) {
  Value v = expr->as.literal.value;
  if (IS_NIL(v)) {
    emit_byte(OP_NIL);
  } else if (IS_BOOL(v)) {
    emit_byte(AS_BOOL(v) ? OP_TRUE : OP_FALSE);
  } else {
    emit_constant(v);
  }
}

static void compile_variable(Expr *expr) {
  ObjString *name = expr->as.variable.name;
  int slot = resolve_local(current, name);
  if (slot != -1) {
    emit_bytes(OP_GET_LOCAL, (U8)slot);
  } else {
    emit_bytes(OP_GET_GLOBAL, identifier_constant(name));
  }
}

static void compile_assign(Expr *expr) {
  compile_expr(expr->as.assign.value);
  ObjString *name = expr->as.assign.name;
  int slot = resolve_local(current, name);
  if (slot != -1) {
    emit_bytes(OP_SET_LOCAL, (U8)slot);
  } else {
    emit_bytes(OP_SET_GLOBAL, identifier_constant(name));
  }
}

static void compile_unary(Expr *expr) {
  compile_expr(expr->as.unary.operand);
  switch (expr->as.unary.op) {
    case TOKEN_MINUS: emit_byte(OP_NEGATE); break;
    case TOKEN_BANG: emit_byte(OP_NOT); break;
    case TOKEN_TILDE: emit_byte(OP_BIT_NOT); break;
    default: compile_error("Unknown unary operator."); break;
  }
}

static void compile_logical(Expr *expr) {
  if (expr->as.logical.op == TOKEN_AMP_AMP) {
    compile_expr(expr->as.logical.left);
    int endJump = emit_jump(OP_JUMP_IF_FALSE);
    emit_byte(OP_POP);
    compile_expr(expr->as.logical.right);
    patch_jump(endJump);
  } else { /* || */
    compile_expr(expr->as.logical.left);
    int endJump = emit_jump(OP_JUMP_IF_TRUE);
    emit_byte(OP_POP);
    compile_expr(expr->as.logical.right);
    patch_jump(endJump);
  }
}

static void compile_call(Expr *expr) {
  compile_expr(expr->as.call.callee);
  int argc = expr->as.call.args.count;
  if (argc > 255) compile_error("Too many call arguments.");
  for (int i = 0; i < argc; i++) compile_expr(expr->as.call.args.items[i]);
  emit_bytes(OP_CALL, (U8)argc);
}

static void compile_array(Expr *expr) {
  int n = expr->as.array.elements.count;
  if (n > 255) compile_error("Array literal too large.");
  for (int i = 0; i < n; i++) compile_expr(expr->as.array.elements.items[i]);
  emit_bytes(OP_ARRAY, (U8)n);
}

static void compile_index(Expr *expr) {
  compile_expr(expr->as.index.object);
  compile_expr(expr->as.index.index);
  emit_byte(OP_INDEX_GET);
}

static void compile_index_set(Expr *expr) {
  compile_expr(expr->as.index_set.object);
  compile_expr(expr->as.index_set.index);
  compile_expr(expr->as.index_set.value);
  emit_byte(OP_INDEX_SET);
}

static void compile_field(Expr *expr) {
  compile_expr(expr->as.field.object);
  emit_bytes(OP_GET_FIELD, identifier_constant(expr->as.field.name));
}

static void compile_field_set(Expr *expr) {
  compile_expr(expr->as.field_set.object);
  compile_expr(expr->as.field_set.value);
  emit_bytes(OP_SET_FIELD, identifier_constant(expr->as.field_set.name));
}

static void compile_expr(Expr *expr) {
  currentLine = expr->line;
  switch (expr->kind) {
    case EXPR_LITERAL: compile_literal(expr); break;
    case EXPR_VARIABLE: compile_variable(expr); break;
    case EXPR_ASSIGN: compile_assign(expr); break;
    case EXPR_UNARY: compile_unary(expr); break;
    case EXPR_LOGICAL: compile_logical(expr); break;
    case EXPR_CALL: compile_call(expr); break;
    case EXPR_ARRAY: compile_array(expr); break;
    case EXPR_INDEX: compile_index(expr); break;
    case EXPR_INDEX_SET: compile_index_set(expr); break;
    case EXPR_FIELD: compile_field(expr); break;
    case EXPR_FIELD_SET: compile_field_set(expr); break;
    case EXPR_BINARY:
      compile_expr(expr->as.binary.left);
      compile_expr(expr->as.binary.right);
      emit_byte(binary_op(expr->as.binary.op));
      break;
  }
}

/* ---- statements ------------------------------------------------------- */

static ObjFunction *compile_function(Stmt *stmt);

static void compile_var(Stmt *stmt) {
  ObjString *name = stmt->as.var.name;
  if (current->scopeDepth == 0) {
    U8 global = identifier_constant(name);
    if (stmt->as.var.init) {
      compile_expr(stmt->as.var.init);
    } else {
      emit_byte(OP_NIL);
    }
    emit_bytes(OP_DEFINE_GLOBAL, global);
  } else {
    /* Compile the initializer before declaring the name so `I64 x = x;`
     * resolves the outer x rather than itself. */
    if (stmt->as.var.init) {
      compile_expr(stmt->as.var.init);
    } else {
      emit_byte(OP_NIL);
    }
    add_local(name);
  }
}

static void compile_block(Stmt *stmt) {
  begin_scope();
  StmtList *list = &stmt->as.block.list;
  for (int i = 0; i < list->count; i++) compile_stmt(list->items[i]);
  end_scope();
}

static void compile_if(Stmt *stmt) {
  compile_expr(stmt->as.iff.cond);
  int thenJump = emit_jump(OP_JUMP_IF_FALSE);
  emit_byte(OP_POP);
  compile_stmt(stmt->as.iff.thenB);
  int elseJump = emit_jump(OP_JUMP);
  patch_jump(thenJump);
  emit_byte(OP_POP);
  if (stmt->as.iff.elseB) compile_stmt(stmt->as.iff.elseB);
  patch_jump(elseJump);
}

static void compile_while(Stmt *stmt) {
  int loopStart = current_chunk()->count;
  Loop loop;
  begin_loop(&loop, loopStart, false);
  compile_expr(stmt->as.whilef.cond);
  int exitJump = emit_jump(OP_JUMP_IF_FALSE);
  emit_byte(OP_POP);
  compile_stmt(stmt->as.whilef.body);
  emit_loop(loopStart);
  patch_jump(exitJump);
  emit_byte(OP_POP);
  end_loop();
}

static void compile_for(Stmt *stmt) {
  begin_scope();
  if (stmt->as.forf.init) compile_stmt(stmt->as.forf.init);

  int loopStart = current_chunk()->count;
  int exitJump = -1;
  if (stmt->as.forf.cond) {
    compile_expr(stmt->as.forf.cond);
    exitJump = emit_jump(OP_JUMP_IF_FALSE);
    emit_byte(OP_POP);
  }

  int continueTarget = loopStart;
  if (stmt->as.forf.incr) {
    int bodyJump = emit_jump(OP_JUMP);
    int incrStart = current_chunk()->count;
    compile_expr(stmt->as.forf.incr);
    emit_byte(OP_POP);
    emit_loop(loopStart);
    loopStart = incrStart;
    continueTarget = incrStart;
    patch_jump(bodyJump);
  }

  Loop loop;
  begin_loop(&loop, continueTarget, false);
  compile_stmt(stmt->as.forf.body);
  emit_loop(loopStart);
  if (exitJump != -1) {
    patch_jump(exitJump);
    emit_byte(OP_POP);
  }
  end_loop();
  end_scope();
}

static void compile_do_while(Stmt *stmt) {
  int loopStart = current_chunk()->count;
  Loop loop;
  begin_loop(&loop, loopStart, false);
  compile_stmt(stmt->as.dowhile.body);
  compile_expr(stmt->as.dowhile.cond);
  int exitJump = emit_jump(OP_JUMP_IF_FALSE);
  emit_byte(OP_POP);
  emit_loop(loopStart);
  patch_jump(exitJump);
  emit_byte(OP_POP);
  end_loop();
}

static void compile_return(Stmt *stmt) {
  if (stmt->as.ret.value) {
    compile_expr(stmt->as.ret.value);
  } else {
    emit_byte(OP_NIL);
  }
  emit_byte(OP_RETURN);
}

static void compile_print(Stmt *stmt) {
  ExprList *args = &stmt->as.print.args;
  if (args->count > 255) compile_error("Too many print arguments.");
  for (int i = 0; i < args->count; i++) compile_expr(args->items[i]);
  emit_bytes(OP_PRINT, (U8)args->count);
}

static void compile_break(Stmt *stmt) {
  (void)stmt;
  Loop *loop = current->loop;
  if (loop == NULL) {
    compile_error("'break' outside of loop or switch.");
    return;
  }
  pop_to(loop->localBase);
  if (loop->breakCount >= MAX_BREAKS) {
    compile_error("Too many 'break' statements in loop.");
    return;
  }
  loop->breaks[loop->breakCount++] = emit_jump(OP_JUMP);
}

static void compile_continue(Stmt *stmt) {
  (void)stmt;
  Loop *loop = current->loop;
  while (loop != NULL && loop->isSwitch) loop = loop->enclosing;
  if (loop == NULL) {
    compile_error("'continue' outside of loop.");
    return;
  }
  pop_to(loop->localBase);
  emit_loop(loop->continueTarget);
}

static void compile_switch(Stmt *stmt) {
  begin_scope();
  compile_expr(stmt->as.switchf.disc);
  int discSlot = current->localCount;
  add_local(NULL); /* discriminant occupies a real stack slot */

  Loop loop;
  begin_loop(&loop, 0, true);

  CaseList *cases = &stmt->as.switchf.cases;
  int *bodyJumps = ALLOCATE(int, cases->count > 0 ? cases->count : 1);

  /* Test sequence: jump to the matching body, leaving the discriminant. */
  for (int i = 0; i < cases->count; i++) {
    if (cases->items[i].value == NULL) {
      bodyJumps[i] = -1; /* default: linked after the test chain */
      continue;
    }
    emit_bytes(OP_GET_LOCAL, (U8)discSlot);
    compile_expr(cases->items[i].value);
    emit_byte(OP_EQUAL);
    int skip = emit_jump(OP_JUMP_IF_FALSE);
    emit_byte(OP_POP); /* matched: discard the comparison result */
    bodyJumps[i] = emit_jump(OP_JUMP);
    patch_jump(skip);
    emit_byte(OP_POP); /* not matched */
  }

  int noMatch = emit_jump(OP_JUMP);

  /* Bodies, emitted in source order; they fall through to the next body. */
  int defaultBody = -1;
  for (int i = 0; i < cases->count; i++) {
    if (cases->items[i].value == NULL) {
      defaultBody = current_chunk()->count;
    } else {
      patch_jump(bodyJumps[i]);
    }
    StmtList *body = &cases->items[i].body;
    for (int j = 0; j < body->count; j++) compile_stmt(body->items[j]);
  }

  if (defaultBody != -1) {
    int jump = defaultBody - noMatch - 2;
    current_chunk()->code[noMatch] = (U8)((jump >> 8) & 0xff);
    current_chunk()->code[noMatch + 1] = (U8)(jump & 0xff);
  } else {
    patch_jump(noMatch);
  }

  FREE_ARRAY(int, bodyJumps, cases->count > 0 ? cases->count : 1);
  end_loop();    /* patches break jumps to here */
  end_scope();   /* pops the discriminant */
}

static void compile_function_decl(Stmt *stmt) {
  ObjFunction *fn = compile_function(stmt);
  emit_bytes(OP_CONSTANT, (U8)make_constant(OBJ_VAL(fn)));
  emit_bytes(OP_DEFINE_GLOBAL, identifier_constant(stmt->as.func.name));
}

static void compile_class_decl(Stmt *stmt) {
  NameList *fields = &stmt->as.klass.fields;
  ObjClass *klass = class_new(stmt->as.klass.name, fields->count);
  for (int i = 0; i < fields->count; i++) klass->fields[i] = fields->items[i];
  emit_bytes(OP_CONSTANT, (U8)make_constant(OBJ_VAL(klass)));
  emit_bytes(OP_DEFINE_GLOBAL, identifier_constant(stmt->as.klass.name));
}

static void compile_stmt(Stmt *stmt) {
  currentLine = stmt->line;
  switch (stmt->kind) {
    case STMT_EXPR:
      compile_expr(stmt->as.expr.expr);
      emit_byte(OP_POP);
      break;
    case STMT_VAR: compile_var(stmt); break;
    case STMT_BLOCK: compile_block(stmt); break;
    case STMT_IF: compile_if(stmt); break;
    case STMT_WHILE: compile_while(stmt); break;
    case STMT_FOR: compile_for(stmt); break;
    case STMT_DOWHILE: compile_do_while(stmt); break;
    case STMT_RETURN: compile_return(stmt); break;
    case STMT_PRINT: compile_print(stmt); break;
    case STMT_BREAK: compile_break(stmt); break;
    case STMT_CONTINUE: compile_continue(stmt); break;
    case STMT_SWITCH: compile_switch(stmt); break;
    case STMT_FUNCTION: compile_function_decl(stmt); break;
    case STMT_CLASS: compile_class_decl(stmt); break;
  }
}

static ObjFunction *compile_function(Stmt *stmt) {
  Compiler compiler;
  ObjFunction *fn = function_new();
  fn->name = stmt->as.func.name;
  fn->arity = stmt->as.func.params.count;
  init_compiler(&compiler, fn, current);

  begin_scope();
  NameList *params = &stmt->as.func.params;
  for (int i = 0; i < params->count; i++) add_local(params->items[i]);
  compile_stmt(stmt->as.func.body);
  emit_return();

  current = compiler.enclosing;
  return fn;
}

ObjFunction *compile_program(StmtList *program) {
  Compiler compiler;
  ObjFunction *script = function_new();
  init_compiler(&compiler, script, NULL);
  hadError = false;

  for (int i = 0; i < program->count; i++) compile_stmt(program->items[i]);
  emit_return();

#ifdef BK_DEBUG_PRINT_CODE
  if (!hadError) disassemble_chunk(current_chunk(), "<script>");
#endif

  current = compiler.enclosing;
  return hadError ? NULL : script;
}
