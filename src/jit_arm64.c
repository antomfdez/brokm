/* jit_arm64.c - arm64/macOS code generator for the baseline JIT.
 *
 * Built only on Apple Silicon (guarded below); jit.c provides the no-backend
 * jit_try_compile elsewhere. Generated code mirrors the bytecode interpreter
 * operation-for-operation on the same VM value stack, so it is correct by
 * construction: integer arithmetic, comparisons, and control flow are inlined
 * as native instructions, while complex/slow operations call back into the
 * interpreter's own C helpers (jit_h_* in vm.c).
 *
 * Conventions in generated code:
 *   x19 = slots base (the call frame: callee at slots[0], args follow)
 *   x20 = cached vm.stackTop; flushed to memory before any helper call and
 *         reloaded after, since helpers push/pop. x9 is an address scratch,
 *         x0..x5/x16 are temporaries.
 * A Value is 16 bytes: the 4-byte type tag in the low word at offset 0, the
 * 8-byte payload at offset 8 (asserted in jit_init). */
#include "jit.h"

#ifdef BK_JIT_ENABLED

#include <libkern/OSCacheControl.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "vm.h" /* the global `vm` (for &vm.stackTop) + value_truthy */

/* ---- registers / conditions ------------------------------------------ */
enum { X0 = 0, X1 = 1, X2 = 2, X3 = 3, X4 = 4, X5 = 5, X9 = 9, X16 = 16,
       X19 = 19, X20 = 20, X29 = 29, X30 = 30, XZR = 31, SP = 31 };
enum { CC_EQ = 0, CC_NE = 1, CC_GE = 10, CC_LT = 11, CC_GT = 12, CC_LE = 13 };

#define VAL_SZ   16 /* sizeof(Value) */
#define OFF_TYPE 0  /* offsetof(Value, type) */
#define OFF_AS   8  /* offsetof(Value, as) */

/* ---- emit buffer ------------------------------------------------------ */
typedef struct {
  U32 *code;
  int count;
  int cap;
  bool oom;
} Emit;

static void emit(Emit *e, U32 inst) {
  if (e->count >= e->cap) {
    int newCap = e->cap < 64 ? 64 : e->cap * 2;
    U32 *grown = (U32 *)realloc(e->code, (size_t)newCap * sizeof(U32));
    if (grown == NULL) { e->oom = true; return; }
    e->code = grown;
    e->cap = newCap;
  }
  e->code[e->count++] = inst;
}

/* ---- instruction encoders (64-bit unless noted) ----------------------- */
static void e_ret(Emit *e, int rn)  { emit(e, 0xD65F0000u | ((U32)rn << 5)); }
static void e_blr(Emit *e, int rn)  { emit(e, 0xD63F0000u | ((U32)rn << 5)); }
static void e_add_imm(Emit *e, int rd, int rn, U32 i) { emit(e, 0x91000000u | (i << 10) | ((U32)rn << 5) | (U32)rd); }
static void e_sub_imm(Emit *e, int rd, int rn, U32 i) { emit(e, 0xD1000000u | (i << 10) | ((U32)rn << 5) | (U32)rd); }
static void e_add_reg(Emit *e, int rd, int rn, int rm) { emit(e, 0x8B000000u | ((U32)rm << 16) | ((U32)rn << 5) | (U32)rd); }
static void e_sub_reg(Emit *e, int rd, int rn, int rm) { emit(e, 0xCB000000u | ((U32)rm << 16) | ((U32)rn << 5) | (U32)rd); }
static void e_mul(Emit *e, int rd, int rn, int rm)  { emit(e, 0x9B000000u | ((U32)rm << 16) | ((U32)XZR << 10) | ((U32)rn << 5) | (U32)rd); }
static void e_sdiv(Emit *e, int rd, int rn, int rm) { emit(e, 0x9AC00C00u | ((U32)rm << 16) | ((U32)rn << 5) | (U32)rd); }
static void e_msub(Emit *e, int rd, int rn, int rm, int ra) { emit(e, 0x9B008000u | ((U32)rm << 16) | ((U32)ra << 10) | ((U32)rn << 5) | (U32)rd); }
static void e_orr(Emit *e, int rd, int rn, int rm)  { emit(e, 0xAA000000u | ((U32)rm << 16) | ((U32)rn << 5) | (U32)rd); }
static void e_mov_reg(Emit *e, int rd, int rm) { e_orr(e, rd, XZR, rm); }

static void e_movz(Emit *e, int rd, U32 imm16, int shift) { emit(e, 0xD2800000u | ((U32)(shift / 16) << 21) | ((imm16 & 0xFFFFu) << 5) | (U32)rd); }
static void e_movk(Emit *e, int rd, U32 imm16, int shift) { emit(e, 0xF2800000u | ((U32)(shift / 16) << 21) | ((imm16 & 0xFFFFu) << 5) | (U32)rd); }
static void e_mov_imm64(Emit *e, int rd, U64 v) {
  e_movz(e, rd, (U32)(v & 0xFFFFu), 0);
  e_movk(e, rd, (U32)((v >> 16) & 0xFFFFu), 16);
  e_movk(e, rd, (U32)((v >> 32) & 0xFFFFu), 32);
  e_movk(e, rd, (U32)((v >> 48) & 0xFFFFu), 48);
}

/* LDUR/STUR: unscaled signed 9-bit byte offset (-256..255). */
static void e_ldur64(Emit *e, int rt, int rn, int off) { emit(e, 0xF8400000u | (((U32)off & 0x1FFu) << 12) | ((U32)rn << 5) | (U32)rt); }
static void e_stur64(Emit *e, int rt, int rn, int off) { emit(e, 0xF8000000u | (((U32)off & 0x1FFu) << 12) | ((U32)rn << 5) | (U32)rt); }
static void e_ldur32(Emit *e, int rt, int rn, int off) { emit(e, 0xB8400000u | (((U32)off & 0x1FFu) << 12) | ((U32)rn << 5) | (U32)rt); }

static void e_cmp_reg(Emit *e, int rn, int rm)   { emit(e, 0xEB00001Fu | ((U32)rm << 16) | ((U32)rn << 5)); }
static void e_cmp_imm32(Emit *e, int rn, U32 i)  { emit(e, 0x7100001Fu | (i << 10) | ((U32)rn << 5)); }
static void e_cset(Emit *e, int rd, int cc) { emit(e, 0x1A9F07E0u | ((U32)(cc ^ 1) << 12) | (U32)rd); }

static void e_stp_pre(Emit *e, int rt, int rt2, int rn, int i) { emit(e, 0xA9800000u | (((U32)(i / 8) & 0x7Fu) << 15) | ((U32)rt2 << 10) | ((U32)rn << 5) | (U32)rt); }
static void e_stp_off(Emit *e, int rt, int rt2, int rn, int i) { emit(e, 0xA9000000u | (((U32)(i / 8) & 0x7Fu) << 15) | ((U32)rt2 << 10) | ((U32)rn << 5) | (U32)rt); }
static void e_ldp_off(Emit *e, int rt, int rt2, int rn, int i) { emit(e, 0xA9400000u | (((U32)(i / 8) & 0x7Fu) << 15) | ((U32)rt2 << 10) | ((U32)rn << 5) | (U32)rt); }
static void e_ldp_post(Emit *e, int rt, int rt2, int rn, int i) { emit(e, 0xA8C00000u | (((U32)(i / 8) & 0x7Fu) << 15) | ((U32)rt2 << 10) | ((U32)rn << 5) | (U32)rt); }

/* Branches emit a placeholder and return the instruction index for patching.
 * Offsets are in instructions, relative to the branch. */
static int e_b(Emit *e)             { emit(e, 0x14000000u); return e->count - 1; }
static int e_bcond(Emit *e, int cc) { emit(e, 0x54000000u | (U32)cc); return e->count - 1; }
static int e_cbz32(Emit *e, int rt) { emit(e, 0x34000000u | (U32)rt); return e->count - 1; }
static int e_cbz64(Emit *e, int rt) { emit(e, 0xB4000000u | (U32)rt); return e->count - 1; }
static int e_cbnz32(Emit *e, int rt){ emit(e, 0x35000000u | (U32)rt); return e->count - 1; }

static void patch_b(Emit *e, int at, int target) {
  e->code[at] = 0x14000000u | ((U32)(target - at) & 0x03FFFFFFu);
}
/* imm19-form branches (b.cond / cbz / cbnz): keep opcode + Rt/cond bits. */
static void patch_imm19(Emit *e, int at, int target) {
  e->code[at] = (e->code[at] & 0xFF00001Fu) | (((U32)(target - at) & 0x7FFFFu) << 5);
}

/* ---- executable page pool -------------------------------------------- */
typedef struct Region {
  U8 *base;
  size_t size;
  size_t used;
  struct Region *next;
} Region;

static Region *g_regions = NULL;
static bool g_jitUsable = false;

static Region *region_new(size_t need) {
  size_t size = 1u << 16;
  if (need > size) size = (need + 0xFFFu) & ~(size_t)0xFFFu;
  void *base = mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                    MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
  if (base == MAP_FAILED) return NULL;
  Region *r = (Region *)malloc(sizeof(Region));
  if (r == NULL) { munmap(base, size); return NULL; }
  r->base = (U8 *)base;
  r->size = size;
  r->used = 0;
  r->next = g_regions;
  g_regions = r;
  return r;
}

static void *publish(const U32 *code, int count) {
  size_t bytes = (size_t)count * 4;
  Region *r = g_regions;
  if (r == NULL || r->size - r->used < bytes) {
    r = region_new(bytes);
    if (r == NULL) return NULL;
  }
  void *dst = r->base + r->used;
  pthread_jit_write_protect_np(0);
  memcpy(dst, code, bytes);
  pthread_jit_write_protect_np(1);
  sys_icache_invalidate(dst, bytes);
  r->used += (bytes + 15u) & ~(size_t)15u;
  return dst;
}

/* ---- emit helpers shared by the self-test and the walker -------------- */
static void emit_flush_top(Emit *e) { e_mov_imm64(e, X9, (U64)(uintptr_t)&vm.stackTop); e_stur64(e, X20, X9, 0); }
static void emit_reload_top(Emit *e) { e_mov_imm64(e, X9, (U64)(uintptr_t)&vm.stackTop); e_ldur64(e, X20, X9, 0); }

/* Push a 16-byte value already split into its two 64-bit words. */
static void emit_push_words(Emit *e, int rWord0, int rWord1) {
  e_stur64(e, rWord0, X20, 0);
  e_stur64(e, rWord1, X20, 8);
  e_add_imm(e, X20, X20, VAL_SZ);
}

/* ---- the compiled-function prologue/epilogue -------------------------- */
static void compiled_prologue(Emit *e) {
  e_stp_pre(e, X29, X30, SP, -32);
  e_stp_off(e, X19, X20, SP, 16);
  e_mov_reg(e, X19, X0);          /* x19 = slots */
  emit_reload_top(e);             /* x20 = vm.stackTop */
}
static void emit_restore_and_ret(Emit *e) {
  e_ldp_off(e, X19, X20, SP, 16);
  e_ldp_post(e, X29, X30, SP, 32);
  e_ret(e, X30);
}

/* ---- growable int vectors for fixups ---------------------------------- */
typedef struct { int *v; int n, cap; } IntVec;
static bool iv_push(IntVec *iv, int x) {
  if (iv->n >= iv->cap) {
    int c = iv->cap < 16 ? 16 : iv->cap * 2;
    int *g = (int *)realloc(iv->v, (size_t)c * sizeof(int));
    if (!g) return false;
    iv->v = g;
    iv->cap = c;
  }
  iv->v[iv->n++] = x;
  return true;
}

/* ---- arithmetic / comparison op descriptor ---------------------------- */
typedef struct {
  bool ok;       /* is this an int-fast-path binary op? */
  bool isCmp;    /* comparison (BOOL result) vs arithmetic (INT result) */
  int sub;       /* arithmetic: 0=add 1=sub 2=mul 3=div 4=mod; compare: cond */
  U8 generic;    /* opcode passed to jit_h_binary on the slow path */
} BinOp;

static BinOp classify(U8 op) {
  switch (op) {
    case OP_ADD: case OP_IADD: return (BinOp){true, false, 0, OP_ADD};
    case OP_SUB: case OP_ISUB: return (BinOp){true, false, 1, OP_SUB};
    case OP_MUL: case OP_IMUL: return (BinOp){true, false, 2, OP_MUL};
    case OP_DIV: case OP_IDIV: return (BinOp){true, false, 3, OP_DIV};
    case OP_MOD: case OP_IMOD: return (BinOp){true, false, 4, OP_MOD};
    case OP_LESS: case OP_ILESS: return (BinOp){true, true, CC_LT, OP_LESS};
    case OP_LESS_EQUAL: case OP_ILESS_EQUAL: return (BinOp){true, true, CC_LE, OP_LESS_EQUAL};
    case OP_GREATER: case OP_IGREATER: return (BinOp){true, true, CC_GT, OP_GREATER};
    case OP_GREATER_EQUAL: case OP_IGREATER_EQUAL: return (BinOp){true, true, CC_GE, OP_GREATER_EQUAL};
    default: return (BinOp){false, false, 0, 0};
  }
}

/* Inline int fast path with a guard that deopts to jit_h_binary. Operands:
 * a at [x20-32] (payload -24), b at [x20-16] (payload -8). */
static void emit_binary(Emit *e, BinOp b, IntVec *errSites) {
  e_ldur32(e, X0, X20, -VAL_SZ * 2 + OFF_TYPE);  /* a.type */
  e_ldur32(e, X1, X20, -VAL_SZ + OFF_TYPE);      /* b.type */
  e_cmp_imm32(e, X0, VAL_INT);
  int sl1 = e_bcond(e, CC_NE);
  e_cmp_imm32(e, X1, VAL_INT);
  int sl2 = e_bcond(e, CC_NE);
  e_ldur64(e, X2, X20, -VAL_SZ * 2 + OFF_AS);    /* a.payload */
  e_ldur64(e, X3, X20, -VAL_SZ + OFF_AS);        /* b.payload */
  int sl3 = -1;
  if (b.sub == 3 || b.sub == 4) sl3 = e_cbz64(e, X3); /* div/mod: b==0 -> slow */
  if (b.isCmp) {
    e_cmp_reg(e, X2, X3);
    e_cset(e, X4, b.sub);
    e_mov_imm64(e, X5, VAL_BOOL);
  } else {
    switch (b.sub) {
      case 0: e_add_reg(e, X4, X2, X3); break;
      case 1: e_sub_reg(e, X4, X2, X3); break;
      case 2: e_mul(e, X4, X2, X3); break;
      case 3: e_sdiv(e, X4, X2, X3); break;
      case 4: e_sdiv(e, X5, X2, X3); e_msub(e, X4, X5, X3, X2); break;
    }
    e_mov_imm64(e, X5, VAL_INT);
  }
  /* store result into a's slot, drop one value */
  e_stur64(e, X5, X20, -VAL_SZ * 2 + OFF_TYPE);
  e_stur64(e, X4, X20, -VAL_SZ * 2 + OFF_AS);
  e_sub_imm(e, X20, X20, VAL_SZ);
  int done = e_b(e);
  /* slow path */
  int slow = e->count;
  patch_imm19(e, sl1, slow);
  patch_imm19(e, sl2, slow);
  if (sl3 >= 0) patch_imm19(e, sl3, slow);
  emit_flush_top(e);
  e_mov_imm64(e, X0, b.generic);
  e_mov_imm64(e, X16, (U64)(uintptr_t)&jit_h_binary);
  e_blr(e, X16);
  iv_push(errSites, e_cbz32(e, X0)); /* false -> error exit */
  emit_reload_top(e);
  patch_b(e, done, e->count);
}

/* Emit `bl helper` with the cached stack-top flushed/reloaded, checking the
 * bool result and recording an error-exit branch when `checkErr`. */
static void emit_helper_call(Emit *e, const void *helper, bool checkErr,
                             IntVec *errSites) {
  emit_flush_top(e);
  e_mov_imm64(e, X16, (U64)(uintptr_t)helper);
  e_blr(e, X16);
  if (checkErr) iv_push(errSites, e_cbz32(e, X0));
  emit_reload_top(e);
}

/* ---- the bytecode -> native walker ------------------------------------ */
static void *compile_chunk(ObjFunction *fn) {
  Chunk *c = &fn->chunk;
  int n = c->count;
  int *nativeAt = (int *)calloc((size_t)n + 1, sizeof(int));
  if (nativeAt == NULL) return NULL;

  Emit e = {0};
  IntVec errSites = {0};
  IntVec jmpAt = {0};   /* emit index of each control-flow branch */
  IntVec jmpTo = {0};   /* its target bytecode offset */
  IntVec jmpUncond = {0};
  bool eligible = true;

  compiled_prologue(&e);

  for (int ip = 0; ip < n && eligible;) {
    nativeAt[ip] = e.count;
    U8 op = c->code[ip++];
    switch (op) {
      case OP_NIL:   e_mov_imm64(&e, X0, VAL_NIL);  e_mov_imm64(&e, X1, 0); emit_push_words(&e, X0, X1); break;
      case OP_TRUE:  e_mov_imm64(&e, X0, VAL_BOOL); e_mov_imm64(&e, X1, 1); emit_push_words(&e, X0, X1); break;
      case OP_FALSE: e_mov_imm64(&e, X0, VAL_BOOL); e_mov_imm64(&e, X1, 0); emit_push_words(&e, X0, X1); break;
      case OP_POP:   e_sub_imm(&e, X20, X20, VAL_SZ); break;
      case OP_CONSTANT: {
        U8 idx = c->code[ip++];
        e_mov_imm64(&e, X9, (U64)(uintptr_t)&c->constants.values[idx]);
        e_ldur64(&e, X0, X9, 0);
        e_ldur64(&e, X1, X9, 8);
        emit_push_words(&e, X0, X1);
        break;
      }
      case OP_GET_LOCAL: {
        U8 slot = c->code[ip++];
        e_add_imm(&e, X9, X19, (U32)slot * VAL_SZ);
        e_ldur64(&e, X0, X9, 0);
        e_ldur64(&e, X1, X9, 8);
        emit_push_words(&e, X0, X1);
        break;
      }
      case OP_SET_LOCAL: {
        U8 slot = c->code[ip++];
        e_ldur64(&e, X0, X20, -VAL_SZ + 0);
        e_ldur64(&e, X1, X20, -VAL_SZ + 8);
        e_add_imm(&e, X9, X19, (U32)slot * VAL_SZ);
        e_stur64(&e, X0, X9, 0);
        e_stur64(&e, X1, X9, 8);
        break;
      }
      case OP_GET_GLOBAL: {
        U8 idx = c->code[ip++];
        e_mov_imm64(&e, X0, (U64)(uintptr_t)AS_STRING(c->constants.values[idx]));
        emit_helper_call(&e, (void *)&jit_h_get_global, true, &errSites);
        break;
      }
      case OP_SET_GLOBAL: {
        U8 idx = c->code[ip++];
        e_mov_imm64(&e, X0, (U64)(uintptr_t)AS_STRING(c->constants.values[idx]));
        emit_helper_call(&e, (void *)&jit_h_set_global, true, &errSites);
        break;
      }
      case OP_EQUAL:     e_mov_imm64(&e, X0, 0); emit_helper_call(&e, (void *)&jit_h_equal, false, &errSites); break;
      case OP_NOT_EQUAL: e_mov_imm64(&e, X0, 1); emit_helper_call(&e, (void *)&jit_h_equal, false, &errSites); break;
      case OP_CALL: {
        U8 argc = c->code[ip++];
        e_mov_imm64(&e, X0, argc);
        emit_helper_call(&e, (void *)&jit_h_call, true, &errSites);
        break;
      }
      case OP_PRINT: {
        U8 argc = c->code[ip++];
        e_mov_imm64(&e, X0, argc);
        emit_helper_call(&e, (void *)&jit_h_print, false, &errSites);
        break;
      }
      case OP_JUMP: case OP_JUMP_IF_FALSE: case OP_JUMP_IF_TRUE: case OP_LOOP: {
        U8 hi = c->code[ip++], lo = c->code[ip++];
        int operand = (hi << 8) | lo;
        int target = (op == OP_LOOP) ? ip - operand : ip + operand;
        int at;
        if (op == OP_JUMP) {
          at = e_b(&e);
          iv_push(&jmpUncond, 1);
        } else if (op == OP_LOOP) {
          at = e_b(&e);
          iv_push(&jmpUncond, 1);
        } else {
          /* truthiness of peek(0) via value_truthy(Value) — Value passes in
           * x0:x1; x19/x20 are callee-saved so survive the call. */
          e_ldur64(&e, X0, X20, -VAL_SZ + 0);
          e_ldur64(&e, X1, X20, -VAL_SZ + 8);
          e_mov_imm64(&e, X16, (U64)(uintptr_t)&value_truthy);
          e_blr(&e, X16);
          at = (op == OP_JUMP_IF_FALSE) ? e_cbz32(&e, X0) : e_cbnz32(&e, X0);
          iv_push(&jmpUncond, 0);
        }
        iv_push(&jmpAt, at);
        iv_push(&jmpTo, target);
        break;
      }
      case OP_RETURN:
        e_ldur64(&e, X0, X20, -VAL_SZ + 0);   /* result words */
        e_ldur64(&e, X1, X20, -VAL_SZ + 8);
        e_stur64(&e, X0, X19, 0);             /* slots[0] = result */
        e_stur64(&e, X1, X19, 8);
        e_add_imm(&e, X2, X19, VAL_SZ);       /* vm.stackTop = slots + 1 */
        e_mov_imm64(&e, X9, (U64)(uintptr_t)&vm.stackTop);
        e_stur64(&e, X2, X9, 0);
        e_movz(&e, X0, 1, 0);                 /* return true */
        emit_restore_and_ret(&e);
        break;
      default: {
        BinOp b = classify(op);
        if (b.ok) {
          emit_binary(&e, b, &errSites);
        } else {
          eligible = false; /* unsupported opcode: fall back to the interpreter */
        }
        break;
      }
    }
  }

  void *entry = NULL;
  if (eligible && !e.oom) {
    /* shared error exit: return false */
    int errLabel = e.count;
    e_movz(&e, X0, 0, 0);
    emit_restore_and_ret(&e);
    for (int i = 0; i < errSites.n; i++) patch_imm19(&e, errSites.v[i], errLabel);
    /* control-flow branches */
    for (int i = 0; i < jmpAt.n; i++) {
      int tgt = nativeAt[jmpTo.v[i]];
      if (jmpUncond.v[i]) patch_b(&e, jmpAt.v[i], tgt);
      else patch_imm19(&e, jmpAt.v[i], tgt);
    }
    if (!e.oom) entry = publish(e.code, e.count);
  }

  free(e.code);
  free(nativeAt);
  free(errSites.v);
  free(jmpAt.v);
  free(jmpTo.v);
  free(jmpUncond.v);
  return entry;
}

/* ---- startup self-test ------------------------------------------------ */
static I64 selftest_helper(I64 a, I64 b) { return a * b + 7; }
typedef I64 (*Fn1)(I64);
typedef I64 (*Fn2)(I64, I64);

static void *build(Emit *e) {
  if (e->oom) { free(e->code); return NULL; }
  void *fn = publish(e->code, e->count);
  free(e->code);
  return fn;
}

static bool selftest(void) {
  { /* f(x) = x + 41 */
    Emit e = {0};
    e_stp_pre(&e, X29, X30, SP, -32);
    e_stp_off(&e, X19, X20, SP, 16);
    e_add_imm(&e, X0, X0, 41);
    e_ldp_off(&e, X19, X20, SP, 16);
    e_ldp_post(&e, X29, X30, SP, 32);
    e_ret(&e, X30);
    Fn1 f = (Fn1)build(&e);
    if (!f || f(2) != 43) return false;
  }
  { /* g(a,b) = a*b - b, and mod via sdiv+msub */
    Emit e = {0};
    e_stp_pre(&e, X29, X30, SP, -32);
    e_sdiv(&e, X2, X0, X1);
    e_msub(&e, X0, X2, X1, X0); /* a % b */
    e_ldp_post(&e, X29, X30, SP, 32);
    e_ret(&e, X30);
    Fn2 g = (Fn2)build(&e);
    if (!g || g(17, 5) != 2 || g(40, 6) != 4) return false;
  }
  { /* call a C helper: c(a,b) = a*b + 7 */
    Emit e = {0};
    e_stp_pre(&e, X29, X30, SP, -32);
    e_mov_imm64(&e, X16, (U64)(uintptr_t)&selftest_helper);
    e_blr(&e, X16);
    e_ldp_post(&e, X29, X30, SP, 32);
    e_ret(&e, X30);
    Fn2 c = (Fn2)build(&e);
    if (!c || c(6, 7) != 49) return false;
  }
  return true;
}

/* ---- public API ------------------------------------------------------- */
void jit_init(void) {
  bool layoutOK = sizeof(Value) == VAL_SZ &&
                  offsetof(Value, type) == OFF_TYPE &&
                  offsetof(Value, as) == OFF_AS && VAL_INT == 2 &&
                  VAL_BOOL == 1 && VAL_NIL == 0;
  g_jitUsable = layoutOK && selftest();
  if (!g_jitUsable) {
    fprintf(stderr, "brokm: JIT unavailable (self-test/layout); interpreter only.\n");
  }
}

void jit_shutdown(void) {
  for (Region *r = g_regions; r != NULL;) {
    Region *next = r->next;
    munmap(r->base, r->size);
    free(r);
    r = next;
  }
  g_regions = NULL;
}

void jit_try_compile(ObjFunction *fn) {
  void *entry = g_jitUsable ? compile_chunk(fn) : NULL;
  if (entry != NULL) fn->nativeCode = entry;
  else fn->jitDisabled = true; /* ineligible/failed: never retried */
  if (getenv("BROKM_JIT_LOG")) {
    fprintf(stderr, "[jit] %s %s\n", fn->name ? fn->name->chars : "<script>",
            entry ? "compiled" : "bailed");
  }
}

#endif /* BK_JIT_ENABLED */
