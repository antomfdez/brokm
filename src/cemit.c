/* cemit.c - AOT back-end: translate compiled bytecode chunks to C source.
 *
 * Design (v0.12): each ObjFunction chunk becomes a C function with the JitFn
 * signature `bool bk_f<i>(Value *slots)` whose body is the interpreter
 * specialized for that chunk — straight-line C per opcode operating on the VM
 * value stack, labels at jump targets, typed OP_I* ops as guarded int fast
 * paths, everything else through the jit_h_* runtime helpers (the same
 * functions the interpreter's own opcode cases run, so the two paths cannot
 * drift). The bootstrap rebuilds each function's constant pool inside its
 * shim ObjFunction's chunk, so the whole object graph is GC-reachable through
 * the script function on the value stack — no bytecode ships in the binary,
 * but rooting works exactly as in the interpreter. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cemit.h"
#include "chunk.h"
#include "object.h"
#include "table.h"
#include "value.h"

/* ---- reachable-object graph ------------------------------------------- */

typedef struct {
  ObjFunction **fns;
  int fnCount, fnCap;
  ObjClass **classes;
  int classCount, classCap;
  bool ok;
} Graph;

static int graph_fn_id(Graph *g, ObjFunction *fn) {
  for (int i = 0; i < g->fnCount; i++)
    if (g->fns[i] == fn) return i;
  return -1;
}

static int graph_class_id(Graph *g, ObjClass *klass) {
  for (int i = 0; i < g->classCount; i++)
    if (g->classes[i] == klass) return i;
  return -1;
}

static void graph_collect(Graph *g, Value v);

static void graph_add_fn(Graph *g, ObjFunction *fn) {
  if (graph_fn_id(g, fn) >= 0) return;
  if (g->fnCount == g->fnCap) {
    g->fnCap = g->fnCap < 8 ? 8 : g->fnCap * 2;
    g->fns = (ObjFunction **)realloc(g->fns, sizeof(ObjFunction *) * (size_t)g->fnCap);
  }
  g->fns[g->fnCount++] = fn;
  for (int i = 0; i < fn->chunk.constants.count; i++) {
    graph_collect(g, fn->chunk.constants.values[i]);
  }
}

static void graph_add_class(Graph *g, ObjClass *klass) {
  if (graph_class_id(g, klass) >= 0) return;
  if (g->classCount == g->classCap) {
    g->classCap = g->classCap < 8 ? 8 : g->classCap * 2;
    g->classes =
        (ObjClass **)realloc(g->classes, sizeof(ObjClass *) * (size_t)g->classCap);
  }
  g->classes[g->classCount++] = klass;
  for (int i = 0; i < klass->methods.capacity; i++) {
    Entry *e = &klass->methods.entries[i];
    if (e->key != NULL) graph_collect(g, e->value);
  }
}

static void graph_collect(Graph *g, Value v) {
  if (!IS_OBJ(v)) return;
  switch (OBJ_TYPE(v)) {
    case OBJ_STRING: return; /* emitted inline as string_copy(...) */
    case OBJ_FUNCTION: graph_add_fn(g, AS_FUNCTION(v)); return;
    case OBJ_CLASS: graph_add_class(g, AS_CLASS(v)); return;
    default:
      fprintf(stderr,
              "brokm build: unsupported constant object (type %d) in pool\n",
              (int)OBJ_TYPE(v));
      g->ok = false;
      return;
  }
}

/* ---- emission helpers --------------------------------------------------- */

/* Write s as a C string literal. Non-printable bytes become 3-digit octal
 * escapes (always 3 digits, so a following digit char cannot extend them). */
static void emit_c_string(FILE *out, const char *s, int length) {
  fputc('"', out);
  for (int i = 0; i < length; i++) {
    unsigned char c = (unsigned char)s[i];
    switch (c) {
      case '"': fputs("\\\"", out); break;
      case '\\': fputs("\\\\", out); break;
      case '\n': fputs("\\n", out); break;
      case '\t': fputs("\\t", out); break;
      case '\r': fputs("\\r", out); break;
      default:
        if (c < 32 || c > 126) {
          fprintf(out, "\\%03o", c);
        } else {
          fputc((char)c, out);
        }
    }
  }
  fputc('"', out);
}

/* Write a C expression that rebuilds `v` at bootstrap time. */
static void emit_value_expr(FILE *out, Graph *g, Value v) {
  switch (v.type) {
    case VAL_NIL: fputs("NIL_VAL", out); break;
    case VAL_BOOL: fprintf(out, "BOOL_VAL(%s)", AS_BOOL(v) ? "true" : "false"); break;
    case VAL_INT:
      if (AS_INT(v) == INT64_MIN) {
        fputs("INT_VAL(-9223372036854775807LL - 1)", out);
      } else {
        fprintf(out, "INT_VAL(%lldLL)", (long long)AS_INT(v));
      }
      break;
    case VAL_FLOAT:
      /* %a hex-float literals round-trip exactly and are valid C99. */
      fprintf(out, "FLOAT_VAL(%a)", AS_FLOAT(v));
      break;
    case VAL_OBJ:
      switch (OBJ_TYPE(v)) {
        case OBJ_STRING: {
          ObjString *s = AS_STRING(v);
          fputs("OBJ_VAL(string_copy(", out);
          emit_c_string(out, s->chars, s->length);
          fprintf(out, ", %d))", s->length);
          break;
        }
        case OBJ_FUNCTION:
          fprintf(out, "OBJ_VAL(g_f%d)", graph_fn_id(g, AS_FUNCTION(v)));
          break;
        case OBJ_CLASS:
          fprintf(out, "OBJ_VAL(g_c%d)", graph_class_id(g, AS_CLASS(v)));
          break;
        default: fputs("NIL_VAL /* unsupported */", out); break;
      }
      break;
    default: fputs("NIL_VAL /* unsupported */", out); break;
  }
}

/* ---- bytecode walking --------------------------------------------------- */

/* Total instruction length (opcode + operands) at `offset`. */
static int instr_len(const U8 *code, int offset) {
  switch (code[offset]) {
    case OP_CONSTANT:
    case OP_DEFINE_GLOBAL:
    case OP_GET_GLOBAL:
    case OP_SET_GLOBAL:
    case OP_GET_LOCAL:
    case OP_SET_LOCAL:
    case OP_CALL:
    case OP_PRINT:
    case OP_ARRAY:
    case OP_GET_FIELD:
    case OP_SET_FIELD:
      return 2;
    case OP_JUMP:
    case OP_JUMP_IF_FALSE:
    case OP_JUMP_IF_TRUE:
    case OP_LOOP:
    case OP_INVOKE:
      return 3;
    default:
      return 1;
  }
}

/* Where does the jump at `offset` land? (16-bit big-endian operand, relative
 * to the instruction end — matches the interpreter's READ_SHORT use.) */
static int jump_target(const U8 *code, int offset) {
  int operand = (code[offset + 1] << 8) | code[offset + 2];
  return code[offset] == OP_LOOP ? offset + 3 - operand : offset + 3 + operand;
}

static bool op_is_jump(U8 op) {
  return op == OP_JUMP || op == OP_JUMP_IF_FALSE || op == OP_JUMP_IF_TRUE ||
         op == OP_LOOP;
}

/* Generic-opcode enum name, for jit_h_binary(...) arguments and comments. */
static const char *generic_op_name(U8 op) {
  switch (op) {
    case OP_ADD: return "OP_ADD";
    case OP_SUB: return "OP_SUB";
    case OP_MUL: return "OP_MUL";
    case OP_DIV: return "OP_DIV";
    case OP_MOD: return "OP_MOD";
    case OP_LESS: return "OP_LESS";
    case OP_LESS_EQUAL: return "OP_LESS_EQUAL";
    case OP_GREATER: return "OP_GREATER";
    case OP_GREATER_EQUAL: return "OP_GREATER_EQUAL";
    case OP_BIT_AND: return "OP_BIT_AND";
    case OP_BIT_OR: return "OP_BIT_OR";
    case OP_BIT_XOR: return "OP_BIT_XOR";
    case OP_SHL: return "OP_SHL";
    case OP_SHR: return "OP_SHR";
    default: return NULL;
  }
}

/* For a typed int-specialized opcode: the C operator of its fast path and the
 * generic opcode it deopts to on a tag-guard miss. */
typedef struct {
  const char *cop;     /* C infix operator */
  U8 generic;          /* fallback opcode for jit_h_binary */
  bool compare;        /* result is BOOL_VAL (else INT_VAL) */
  bool guardNonZero;   /* IDIV/IMOD: fall to the generic path on b == 0 */
} TypedOp;

static bool typed_op(U8 op, TypedOp *t) {
  switch (op) {
    case OP_IADD: *t = (TypedOp){"+", OP_ADD, false, false}; return true;
    case OP_ISUB: *t = (TypedOp){"-", OP_SUB, false, false}; return true;
    case OP_IMUL: *t = (TypedOp){"*", OP_MUL, false, false}; return true;
    case OP_IDIV: *t = (TypedOp){"/", OP_DIV, false, true}; return true;
    case OP_IMOD: *t = (TypedOp){"%", OP_MOD, false, true}; return true;
    case OP_ILESS: *t = (TypedOp){"<", OP_LESS, true, false}; return true;
    case OP_ILESS_EQUAL: *t = (TypedOp){"<=", OP_LESS_EQUAL, true, false}; return true;
    case OP_IGREATER: *t = (TypedOp){">", OP_GREATER, true, false}; return true;
    case OP_IGREATER_EQUAL:
      *t = (TypedOp){">=", OP_GREATER_EQUAL, true, false};
      return true;
    default: return false;
  }
}

/* ---- per-function body emission ----------------------------------------- */

static bool emit_fn_body(FILE *out, Graph *g, int id) {
  ObjFunction *fn = g->fns[id];
  Chunk *c = &fn->chunk;

  /* Pass 1: collect jump-target offsets so only used labels are emitted. */
  bool *isTarget = (bool *)calloc((size_t)c->count + 1, sizeof(bool));
  for (int o = 0; o < c->count; o += instr_len(c->code, o)) {
    if (op_is_jump(c->code[o])) {
      int t = jump_target(c->code, o);
      if (t < 0 || t > c->count) {
        fprintf(stderr, "brokm build: jump out of range in %s\n",
                fn->name ? fn->name->chars : "<script>");
        free(isTarget);
        return false;
      }
      isTarget[t] = true;
    }
  }

  fprintf(out, "/* ---- %s (arity %d) ---- */\n",
          fn->name ? fn->name->chars : "<script>", fn->arity);
  fprintf(out, "static bool bk_f%d(Value *slots) {\n", id);
  if (c->constants.count > 0) {
    fprintf(out, "  const Value *K = g_f%d->chunk.constants.values;\n", id);
  }

  /* Pass 2: one C statement (or block) per instruction. */
  for (int o = 0; o < c->count; o += instr_len(c->code, o)) {
    if (isTarget[o]) fprintf(out, "L_%04d:\n", o);
    U8 op = c->code[o];

    TypedOp t;
    const char *gname;
    if (typed_op(op, &t)) {
      /* Guarded int fast path; deopt to the generic helper on a miss, which
       * preserves string concat, int->float promotion, and div0 reporting —
       * mirrors the interpreter's OP_I* cases (vm.c). */
      fprintf(out, "  if (IS_INT(PEEK(0)) && IS_INT(PEEK(1))%s) {\n",
              t.guardNonZero ? " && AS_INT(PEEK(0)) != 0" : "");
      fprintf(out, "    Value b = POP(), a = POP();\n");
      if (t.compare) {
        fprintf(out, "    PUSH(BOOL_VAL(AS_INT(a) %s AS_INT(b)));\n", t.cop);
      } else {
        fprintf(out, "    PUSH(INT_VAL(AS_INT(a) %s AS_INT(b)));\n", t.cop);
      }
      fprintf(out, "  } else if (!jit_h_binary(%s)) return false;\n",
              generic_op_name(t.generic));
      continue;
    }

    switch (op) {
      case OP_CONSTANT:
        fprintf(out, "  PUSH(K[%d]);\n", c->code[o + 1]);
        break;
      case OP_NIL: fputs("  PUSH(NIL_VAL);\n", out); break;
      case OP_TRUE: fputs("  PUSH(BOOL_VAL(true));\n", out); break;
      case OP_FALSE: fputs("  PUSH(BOOL_VAL(false));\n", out); break;
      case OP_POP: fputs("  (void)POP();\n", out); break;

      case OP_DEFINE_GLOBAL:
        fprintf(out, "  jit_h_define_global(AS_STRING(K[%d]));\n", c->code[o + 1]);
        break;
      case OP_GET_GLOBAL:
        fprintf(out, "  if (!jit_h_get_global(AS_STRING(K[%d]))) return false;\n",
                c->code[o + 1]);
        break;
      case OP_SET_GLOBAL:
        fprintf(out, "  if (!jit_h_set_global(AS_STRING(K[%d]))) return false;\n",
                c->code[o + 1]);
        break;
      case OP_GET_LOCAL:
        fprintf(out, "  PUSH(slots[%d]);\n", c->code[o + 1]);
        break;
      case OP_SET_LOCAL:
        fprintf(out, "  slots[%d] = PEEK(0);\n", c->code[o + 1]);
        break;

      case OP_EQUAL: fputs("  jit_h_equal(0);\n", out); break;
      case OP_NOT_EQUAL: fputs("  jit_h_equal(1);\n", out); break;

      case OP_ADD:
      case OP_SUB:
      case OP_MUL:
      case OP_DIV:
      case OP_MOD:
      case OP_LESS:
      case OP_LESS_EQUAL:
      case OP_GREATER:
      case OP_GREATER_EQUAL:
      case OP_BIT_AND:
      case OP_BIT_OR:
      case OP_BIT_XOR:
      case OP_SHL:
      case OP_SHR:
        gname = generic_op_name(op);
        fprintf(out, "  if (!jit_h_binary(%s)) return false;\n", gname);
        break;

      case OP_NEGATE: fputs("  if (!jit_h_negate()) return false;\n", out); break;
      case OP_NOT:
        fputs("  PUSH(BOOL_VAL(!value_truthy(POP())));\n", out);
        break;
      case OP_BIT_NOT: fputs("  if (!jit_h_bit_not()) return false;\n", out); break;

      case OP_JUMP:
        fprintf(out, "  goto L_%04d;\n", jump_target(c->code, o));
        break;
      case OP_JUMP_IF_FALSE:
        fprintf(out, "  if (!value_truthy(PEEK(0))) goto L_%04d;\n",
                jump_target(c->code, o));
        break;
      case OP_JUMP_IF_TRUE:
        fprintf(out, "  if (value_truthy(PEEK(0))) goto L_%04d;\n",
                jump_target(c->code, o));
        break;
      case OP_LOOP:
        fprintf(out, "  goto L_%04d;\n", jump_target(c->code, o));
        break;

      case OP_CALL:
        fprintf(out, "  if (!jit_h_call(%d)) return false;\n", c->code[o + 1]);
        break;
      case OP_INVOKE:
        fprintf(out, "  if (!jit_h_invoke(AS_STRING(K[%d]), %d)) return false;\n",
                c->code[o + 1], c->code[o + 2]);
        break;
      case OP_PRINT:
        fprintf(out, "  jit_h_print(%d);\n", c->code[o + 1]);
        break;

      case OP_ARRAY:
        fprintf(out, "  jit_h_array(%d);\n", c->code[o + 1]);
        break;
      case OP_INDEX_GET:
        fputs("  if (!jit_h_index_get()) return false;\n", out);
        break;
      case OP_INDEX_SET:
        fputs("  if (!jit_h_index_set()) return false;\n", out);
        break;
      case OP_GET_FIELD:
        fprintf(out, "  if (!jit_h_get_field(AS_STRING(K[%d]))) return false;\n",
                c->code[o + 1]);
        break;
      case OP_SET_FIELD:
        fprintf(out, "  if (!jit_h_set_field(AS_STRING(K[%d]))) return false;\n",
                c->code[o + 1]);
        break;

      case OP_RETURN:
        /* Same epilogue as the interpreter's OP_RETURN for a callee frame:
         * discard the window, leave the result for the caller. */
        fputs("  { Value r = POP(); vm->stackTop = slots; PUSH(r); return true; }\n",
              out);
        break;

      default:
        fprintf(stderr, "brokm build: cannot translate opcode %d in %s\n", (int)op,
                fn->name ? fn->name->chars : "<script>");
        free(isTarget);
        return false;
    }
  }

  fputs("}\n\n", out);
  free(isTarget);
  return true;
}

/* ---- whole-program emission --------------------------------------------- */

bool cemit_program(ObjFunction *script, FILE *out) {
  Graph g = {0};
  g.ok = true;
  graph_add_fn(&g, script); /* id 0 = the script */
  if (!g.ok) goto fail;

  fputs("/* Generated by brokm build -- do not edit. */\n", out);
  fputs("#include <stdio.h>\n\n#include \"jit.h\"\n#include \"vm.h\"\n\n", out);
  fputs("static inline void PUSH(Value v) { *vm->stackTop++ = v; }\n", out);
  fputs("static inline Value POP(void) { return *(--vm->stackTop); }\n", out);
  fputs("static inline Value PEEK(int d) { return vm->stackTop[-1 - d]; }\n\n", out);

  for (int i = 0; i < g.fnCount; i++) {
    fprintf(out, "static ObjFunction *g_f%d; /* %s */\n", i,
            g.fns[i]->name ? g.fns[i]->name->chars : "<script>");
  }
  for (int i = 0; i < g.classCount; i++) {
    fprintf(out, "static ObjClass *g_c%d; /* %s */\n", i,
            g.classes[i]->name->chars);
  }
  fputs("\n", out);
  for (int i = 0; i < g.fnCount; i++) {
    fprintf(out, "static bool bk_f%d(Value *slots);\n", i);
  }
  fputs("\n", out);

  for (int i = 0; i < g.fnCount; i++) {
    if (!emit_fn_body(out, &g, i)) goto fail;
  }

  /* Bootstrap: rebuild the constant graph. Runs with the GC disabled (vm_new
   * leaves gcEnabled false), so ordinary stores need no rooting or barriers;
   * the graph becomes reachable when main() pushes the script function. */
  fputs("static void bk_bootstrap(void) {\n", out);
  for (int i = 0; i < g.fnCount; i++) {
    fprintf(out, "  g_f%d = function_new();\n", i);
  }
  for (int i = 0; i < g.fnCount; i++) {
    ObjFunction *fn = g.fns[i];
    if (fn->name != NULL) {
      fprintf(out, "  g_f%d->name = string_copy(", i);
      emit_c_string(out, fn->name->chars, fn->name->length);
      fprintf(out, ", %d);\n", fn->name->length);
    }
    if (fn->arity != 0) fprintf(out, "  g_f%d->arity = %d;\n", i, fn->arity);
    fprintf(out, "  g_f%d->nativeCode = (void *)bk_f%d;\n", i, i);
    fprintf(out, "  g_f%d->jitDisabled = true;\n", i);
  }
  for (int i = 0; i < g.classCount; i++) {
    ObjClass *klass = g.classes[i];
    fprintf(out, "  g_c%d = class_new(string_copy(", i);
    emit_c_string(out, klass->name->chars, klass->name->length);
    fprintf(out, ", %d), %d);\n", klass->name->length, klass->fieldCount);
    for (int f = 0; f < klass->fieldCount; f++) {
      fprintf(out, "  g_c%d->fields[%d] = string_copy(", i, f);
      emit_c_string(out, klass->fields[f]->chars, klass->fields[f]->length);
      fprintf(out, ", %d);\n", klass->fields[f]->length);
    }
    for (int e = 0; e < klass->methods.capacity; e++) {
      Entry *entry = &klass->methods.entries[e];
      if (entry->key == NULL) continue;
      fprintf(out, "  table_set(&g_c%d->methods, string_copy(", i);
      emit_c_string(out, entry->key->chars, entry->key->length);
      fprintf(out, ", %d), ", entry->key->length);
      emit_value_expr(out, &g, entry->value);
      fputs(");\n", out);
    }
  }
  for (int i = 0; i < g.fnCount; i++) {
    ObjFunction *fn = g.fns[i];
    for (int k = 0; k < fn->chunk.constants.count; k++) {
      fprintf(out, "  chunk_add_constant(&g_f%d->chunk, ", i);
      emit_value_expr(out, &g, fn->chunk.constants.values[k]);
      fputs(");\n", out);
    }
  }
  fputs("}\n\n", out);

  fputs("int main(void) {\n"
        "  VM *v = vm_new();\n"
        "  if (v == NULL) {\n"
        "    fprintf(stderr, \"brokm: out of memory\\n\");\n"
        "    return 70;\n"
        "  }\n"
        "  bk_bootstrap();\n"
        "  vm_push(OBJ_VAL(g_f0)); /* roots the whole constant graph */\n"
        "  vm->gcEnabled = true;\n"
        "  bool ok = bk_f0(vm->stackTop - 1);\n"
        "  vm_destroy(v);\n"
        "  return ok ? 0 : 70;\n"
        "}\n",
        out);

  free(g.fns);
  free(g.classes);
  return true;

fail:
  free(g.fns);
  free(g.classes);
  return false;
}
