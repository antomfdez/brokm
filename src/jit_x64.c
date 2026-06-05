/* jit_x64.c - x86-64 (SysV AMD64) code generator for the baseline JIT.
 *
 * Built only on x86-64 (guarded below). Mirrors jit_arm64.c exactly: the
 * generated code runs the bytecode interpreter operation-for-operation on the
 * same VM value stack, so it is correct by construction. Integer arithmetic,
 * comparisons, and control flow are inlined as native instructions; complex/
 * slow operations call back into the interpreter's C helpers (jit_h_* in vm.c).
 *
 * Conventions in generated code:
 *   r14 = slots base (callee at slots[0], args follow)
 *   r15 = cached vm.stackTop; flushed to memory before any helper call and
 *         reloaded after. r11 holds baked addresses; rax/rcx/rdx/r10 compute.
 * A Value is 16 bytes: the 4-byte type tag at offset 0, the 8-byte payload at
 * offset 8 (asserted in the shared jit_init). */
#include "jit.h"

#if defined(BK_JIT_ENABLED) && defined(__x86_64__)

#include <stdlib.h>

#include "vm.h" /* the global `vm` (for &vm.stackTop) + value_truthy */

/* ---- registers / condition codes -------------------------------------- */
enum { RAX = 0, RCX = 1, RDX = 2, RSI = 6, RDI = 7, R10 = 10, R11 = 11,
       RBP = 5, R14 = 14, R15 = 15 };
enum { SLOTS = R14, STKTOP = R15, ADDR = R11 };
/* jcc/setcc condition encodings (low nibble of the opcode). */
enum { CC_E = 0x4, CC_NE = 0x5, CC_L = 0xC, CC_GE = 0xD, CC_LE = 0xE, CC_G = 0xF };

#define VAL_SZ 16
#define A_TYPE (-32)
#define A_PAY (-24)
#define B_TYPE (-16)
#define B_PAY (-8)

/* ---- emit buffer (variable-length bytes) ------------------------------ */
typedef struct {
  U8 *code;
  int count;
  int cap;
  bool oom;
} Emit;

static void e_u8(Emit *e, U8 b) {
  if (e->count >= e->cap) {
    int nc = e->cap < 128 ? 128 : e->cap * 2;
    U8 *g = (U8 *)realloc(e->code, (size_t)nc);
    if (!g) { e->oom = true; return; }
    e->code = g;
    e->cap = nc;
  }
  e->code[e->count++] = b;
}
static void e_u32(Emit *e, U32 v) { e_u8(e, v & 0xFF); e_u8(e, (v >> 8) & 0xFF); e_u8(e, (v >> 16) & 0xFF); e_u8(e, (v >> 24) & 0xFF); }
static void e_u64(Emit *e, U64 v) { e_u32(e, (U32)v); e_u32(e, (U32)(v >> 32)); }

/* ---- instruction encoders --------------------------------------------- */
static void rex(Emit *e, int w, int r, int b) {
  if (w || (r >= 8) || (b >= 8)) e_u8(e, 0x40 | (w << 3) | ((r >= 8) << 2) | (b >= 8));
}
static void modrm(Emit *e, int mod, int reg, int rm) { e_u8(e, (U8)((mod << 6) | ((reg & 7) << 3) | (rm & 7))); }

static void mov_rr(Emit *e, int dst, int src) { rex(e, 1, src, dst); e_u8(e, 0x89); modrm(e, 3, src, dst); }
static void movabs(Emit *e, int reg, U64 imm) { rex(e, 1, 0, reg); e_u8(e, 0xB8 | (reg & 7)); e_u64(e, imm); }
static void mov_imm32(Emit *e, int reg, U32 imm) { rex(e, 0, 0, reg); e_u8(e, 0xB8 | (reg & 7)); e_u32(e, imm); } /* zero-extends to 64 */

/* 64-bit and 32-bit loads/stores with a disp32 [base+disp]. base is r14/r15/r10
 * /r11 (low3 never 100/101), so no SIB and no RIP-relative special case. */
static void ld(Emit *e, int dst, int base, int disp, int w) { rex(e, w, dst, base); e_u8(e, 0x8B); modrm(e, 2, dst, base); e_u32(e, (U32)disp); }
static void st(Emit *e, int src, int base, int disp, int w) { rex(e, w, src, base); e_u8(e, 0x89); modrm(e, 2, src, base); e_u32(e, (U32)disp); }
/* mov qword [base+disp], imm32 (sign-extended): zeroes the 4 padding bytes. */
static void st_imm(Emit *e, int base, int disp, U32 imm) { rex(e, 1, 0, base); e_u8(e, 0xC7); modrm(e, 2, 0, base); e_u32(e, (U32)disp); e_u32(e, imm); }

static void alu_rr(Emit *e, U8 op, int dst, int src) { rex(e, 1, src, dst); e_u8(e, op); modrm(e, 3, src, dst); }
static void e_add(Emit *e, int dst, int src) { alu_rr(e, 0x01, dst, src); }
static void e_sub(Emit *e, int dst, int src) { alu_rr(e, 0x29, dst, src); }
static void e_cmp(Emit *e, int lhs, int rhs) { alu_rr(e, 0x39, lhs, rhs); }
static void e_imul(Emit *e, int dst, int src) { rex(e, 1, dst, src); e_u8(e, 0x0F); e_u8(e, 0xAF); modrm(e, 3, dst, src); }
static void e_cqo(Emit *e) { e_u8(e, 0x48); e_u8(e, 0x99); }
static void e_idiv(Emit *e, int reg) { rex(e, 1, 0, reg); e_u8(e, 0xF7); modrm(e, 3, 7, reg); }

static void cmp_imm8(Emit *e, int reg, int imm, int w) { rex(e, w, 0, reg); e_u8(e, 0x83); modrm(e, 3, 7, reg); e_u8(e, (U8)imm); }
static void add_imm8(Emit *e, int reg, int imm) { rex(e, 1, 0, reg); e_u8(e, 0x83); modrm(e, 3, 0, reg); e_u8(e, (U8)imm); }
static void sub_imm8(Emit *e, int reg, int imm) { rex(e, 1, 0, reg); e_u8(e, 0x83); modrm(e, 3, 5, reg); e_u8(e, (U8)imm); }
static void e_lea(Emit *e, int dst, int base, int disp) { rex(e, 1, dst, base); e_u8(e, 0x8D); modrm(e, 2, dst, base); e_u32(e, (U32)disp); }

static void e_setcc(Emit *e, int cc, int reg) { rex(e, 0, 0, reg); e_u8(e, 0x0F); e_u8(e, (U8)(0x90 | cc)); modrm(e, 3, 0, reg); }
static void e_movzx8(Emit *e, int dst, int src) { rex(e, 1, dst, src); e_u8(e, 0x0F); e_u8(e, 0xB6); modrm(e, 3, dst, src); }
static void e_test(Emit *e, int a, int b) { rex(e, 0, b, a); e_u8(e, 0x85); modrm(e, 3, b, a); } /* test r32,r32 */
static void e_xor32(Emit *e, int reg) { rex(e, 0, reg, reg); e_u8(e, 0x31); modrm(e, 3, reg, reg); }

static void e_push(Emit *e, int reg) { if (reg >= 8) e_u8(e, 0x41); e_u8(e, (U8)(0x50 | (reg & 7))); }
static void e_pop(Emit *e, int reg) { if (reg >= 8) e_u8(e, 0x41); e_u8(e, (U8)(0x58 | (reg & 7))); }
static void e_call(Emit *e, int reg) { if (reg >= 8) e_u8(e, 0x41); e_u8(e, 0xFF); modrm(e, 3, 2, reg); }
static void e_ret(Emit *e) { e_u8(e, 0xC3); }

/* jmp/jcc rel32: emit a placeholder and return the displacement's byte offset;
 * patch_rel writes the final relative target (relative to the instruction end). */
static int e_jmp(Emit *e) { e_u8(e, 0xE9); int at = e->count; e_u32(e, 0); return at; }
static int e_jcc(Emit *e, int cc) { e_u8(e, 0x0F); e_u8(e, (U8)(0x80 | cc)); int at = e->count; e_u32(e, 0); return at; }
static void patch_rel(Emit *e, int at, int target) {
  U32 rel = (U32)(target - (at + 4));
  e->code[at] = rel & 0xFF; e->code[at + 1] = (rel >> 8) & 0xFF;
  e->code[at + 2] = (rel >> 16) & 0xFF; e->code[at + 3] = (rel >> 24) & 0xFF;
}

/* ---- shared helpers ---------------------------------------------------- */
static void emit_flush_top(Emit *e) { movabs(e, ADDR, (U64)(uintptr_t)&vm.stackTop); st(e, STKTOP, ADDR, 0, 1); }
static void emit_reload_top(Emit *e) { movabs(e, ADDR, (U64)(uintptr_t)&vm.stackTop); ld(e, STKTOP, ADDR, 0, 1); }

/* push a 16-byte value already in rax (word0) / rcx (word1) */
static void emit_push_rax_rcx(Emit *e) {
  st(e, RAX, STKTOP, 0, 1);
  st(e, RCX, STKTOP, 8, 1);
  add_imm8(e, STKTOP, VAL_SZ);
}

static void compiled_prologue(Emit *e) {
  e_push(e, RBP);
  e_push(e, R14);
  e_push(e, R15);            /* rsp now 16-byte aligned for calls */
  mov_rr(e, SLOTS, RDI);     /* r14 = slots */
  emit_reload_top(e);        /* r15 = vm.stackTop */
}
static void emit_restore_and_ret(Emit *e) {
  e_pop(e, R15);
  e_pop(e, R14);
  e_pop(e, RBP);
  e_ret(e);
}

/* ---- growable int vectors (branch/error fixups) ----------------------- */
typedef struct { int *v; int n, cap; } IntVec;
static bool iv_push(IntVec *iv, int x) {
  if (iv->n >= iv->cap) {
    int c = iv->cap < 16 ? 16 : iv->cap * 2;
    int *g = (int *)realloc(iv->v, (size_t)c * sizeof(int));
    if (!g) return false;
    iv->v = g; iv->cap = c;
  }
  iv->v[iv->n++] = x;
  return true;
}

/* ---- arithmetic / comparison descriptor ------------------------------- */
typedef struct { bool ok; bool isCmp; int sub; U8 generic; } BinOp;
static BinOp classify(U8 op) {
  switch (op) {
    case OP_ADD: case OP_IADD: return (BinOp){true, false, 0, OP_ADD};
    case OP_SUB: case OP_ISUB: return (BinOp){true, false, 1, OP_SUB};
    case OP_MUL: case OP_IMUL: return (BinOp){true, false, 2, OP_MUL};
    case OP_DIV: case OP_IDIV: return (BinOp){true, false, 3, OP_DIV};
    case OP_MOD: case OP_IMOD: return (BinOp){true, false, 4, OP_MOD};
    case OP_LESS: case OP_ILESS: return (BinOp){true, true, CC_L, OP_LESS};
    case OP_LESS_EQUAL: case OP_ILESS_EQUAL: return (BinOp){true, true, CC_LE, OP_LESS_EQUAL};
    case OP_GREATER: case OP_IGREATER: return (BinOp){true, true, CC_G, OP_GREATER};
    case OP_GREATER_EQUAL: case OP_IGREATER_EQUAL: return (BinOp){true, true, CC_GE, OP_GREATER_EQUAL};
    default: return (BinOp){false, false, 0, 0};
  }
}

/* Inline int fast path (operands a at [r15-32], b at [r15-16]) with a tag guard
 * that deopts to jit_h_binary. */
static void emit_binary(Emit *e, BinOp b, IntVec *errSites) {
  ld(e, RAX, STKTOP, A_TYPE, 0);          /* eax = a.type */
  ld(e, RCX, STKTOP, B_TYPE, 0);          /* ecx = b.type */
  cmp_imm8(e, RAX, VAL_INT, 0);
  int sl1 = e_jcc(e, CC_NE);
  cmp_imm8(e, RCX, VAL_INT, 0);
  int sl2 = e_jcc(e, CC_NE);
  ld(e, RAX, STKTOP, A_PAY, 1);           /* rax = a.payload */
  ld(e, RCX, STKTOP, B_PAY, 1);           /* rcx = b.payload */
  int sl3 = -1;
  if (b.sub == 3 || b.sub == 4) { cmp_imm8(e, RCX, 0, 1); sl3 = e_jcc(e, CC_E); } /* div0 -> slow */
  if (b.isCmp) {
    e_cmp(e, RAX, RCX);
    e_setcc(e, b.sub, RAX);               /* al = (cond) */
    e_movzx8(e, RAX, RAX);                /* rax = 0/1 */
    st_imm(e, STKTOP, A_TYPE, VAL_BOOL);
  } else {
    switch (b.sub) {
      case 0: e_add(e, RAX, RCX); break;
      case 1: e_sub(e, RAX, RCX); break;
      case 2: e_imul(e, RAX, RCX); break;
      case 3: e_cqo(e); e_idiv(e, RCX); break;             /* quotient in rax */
      case 4: e_cqo(e); e_idiv(e, RCX); mov_rr(e, RAX, RDX); break; /* remainder */
    }
    st_imm(e, STKTOP, A_TYPE, VAL_INT);
  }
  st(e, RAX, STKTOP, A_PAY, 1);           /* store result payload into a's slot */
  sub_imm8(e, STKTOP, VAL_SZ);            /* drop one value */
  int done = e_jmp(e);
  /* slow path */
  int slow = e->count;
  patch_rel(e, sl1, slow);
  patch_rel(e, sl2, slow);
  if (sl3 >= 0) patch_rel(e, sl3, slow);
  emit_flush_top(e);
  mov_imm32(e, RDI, b.generic);
  movabs(e, ADDR, (U64)(uintptr_t)&jit_h_binary);
  e_call(e, ADDR);
  e_test(e, RAX, RAX);
  iv_push(errSites, e_jcc(e, CC_E));      /* false -> error exit */
  emit_reload_top(e);
  patch_rel(e, done, e->count);
}

/* bl helper with the cached stack-top flushed/reloaded; optional error check. */
static void emit_helper_call(Emit *e, const void *helper, bool checkErr, IntVec *errSites) {
  emit_flush_top(e);
  movabs(e, ADDR, (U64)(uintptr_t)helper);
  e_call(e, ADDR);
  if (checkErr) { e_test(e, RAX, RAX); iv_push(errSites, e_jcc(e, CC_E)); }
  emit_reload_top(e);
}

/* ---- the bytecode -> native walker ------------------------------------ */
static void *compile_chunk(ObjFunction *fn) {
  Chunk *c = &fn->chunk;
  int n = c->count;
  int *nativeAt = (int *)calloc((size_t)n + 1, sizeof(int));
  if (nativeAt == NULL) return NULL;

  Emit e = {0};
  IntVec errSites = {0}, jmpAt = {0}, jmpTo = {0};
  bool eligible = true;

  compiled_prologue(&e);

  for (int ip = 0; ip < n && eligible;) {
    nativeAt[ip] = e.count;
    U8 op = c->code[ip++];
    switch (op) {
      case OP_NIL:   movabs(&e, RAX, 0); movabs(&e, RCX, 0); emit_push_rax_rcx(&e); break;
      case OP_TRUE:  movabs(&e, RAX, VAL_BOOL); movabs(&e, RCX, 1); emit_push_rax_rcx(&e); break;
      case OP_FALSE: movabs(&e, RAX, VAL_BOOL); movabs(&e, RCX, 0); emit_push_rax_rcx(&e); break;
      case OP_POP:   sub_imm8(&e, STKTOP, VAL_SZ); break;
      case OP_CONSTANT: {
        U8 idx = c->code[ip++];
        movabs(&e, R10, (U64)(uintptr_t)&c->constants.values[idx]);
        ld(&e, RAX, R10, 0, 1);
        ld(&e, RCX, R10, 8, 1);
        emit_push_rax_rcx(&e);
        break;
      }
      case OP_GET_LOCAL: {
        U8 slot = c->code[ip++];
        e_lea(&e, R10, SLOTS, slot * VAL_SZ); /* &slots[slot] */
        ld(&e, RAX, R10, 0, 1);
        ld(&e, RCX, R10, 8, 1);
        emit_push_rax_rcx(&e);
        break;
      }
      case OP_SET_LOCAL: {
        U8 slot = c->code[ip++];
        ld(&e, RAX, STKTOP, -VAL_SZ, 1);      /* top value (peek) */
        ld(&e, RCX, STKTOP, -VAL_SZ + 8, 1);
        e_lea(&e, R10, SLOTS, slot * VAL_SZ);
        st(&e, RAX, R10, 0, 1);
        st(&e, RCX, R10, 8, 1);
        break;
      }
      case OP_GET_GLOBAL: {
        U8 idx = c->code[ip++];
        movabs(&e, RDI, (U64)(uintptr_t)AS_STRING(c->constants.values[idx]));
        emit_helper_call(&e, (void *)&jit_h_get_global, true, &errSites);
        break;
      }
      case OP_SET_GLOBAL: {
        U8 idx = c->code[ip++];
        movabs(&e, RDI, (U64)(uintptr_t)AS_STRING(c->constants.values[idx]));
        emit_helper_call(&e, (void *)&jit_h_set_global, true, &errSites);
        break;
      }
      case OP_EQUAL:     mov_imm32(&e, RDI, 0); emit_helper_call(&e, (void *)&jit_h_equal, false, &errSites); break;
      case OP_NOT_EQUAL: mov_imm32(&e, RDI, 1); emit_helper_call(&e, (void *)&jit_h_equal, false, &errSites); break;
      case OP_CALL: {
        U8 argc = c->code[ip++];
        mov_imm32(&e, RDI, argc);
        emit_helper_call(&e, (void *)&jit_h_call, true, &errSites);
        break;
      }
      case OP_PRINT: {
        U8 argc = c->code[ip++];
        mov_imm32(&e, RDI, argc);
        emit_helper_call(&e, (void *)&jit_h_print, false, &errSites);
        break;
      }
      case OP_JUMP: case OP_JUMP_IF_FALSE: case OP_JUMP_IF_TRUE: case OP_LOOP: {
        U8 hi = c->code[ip++], lo = c->code[ip++];
        int operand = (hi << 8) | lo;
        int target = (op == OP_LOOP) ? ip - operand : ip + operand;
        int at;
        if (op == OP_JUMP || op == OP_LOOP) {
          at = e_jmp(&e);
        } else {
          /* truthiness of peek(0) via value_truthy(Value): Value passes in
           * rdi:rsi; r14/r15 are callee-saved so survive the call. */
          ld(&e, RDI, STKTOP, -VAL_SZ, 1);
          ld(&e, RSI, STKTOP, -VAL_SZ + 8, 1);
          movabs(&e, ADDR, (U64)(uintptr_t)&value_truthy);
          e_call(&e, ADDR);
          e_test(&e, RAX, RAX);
          at = e_jcc(&e, op == OP_JUMP_IF_FALSE ? CC_E : CC_NE);
        }
        iv_push(&jmpAt, at);
        iv_push(&jmpTo, target);
        break;
      }
      case OP_RETURN:
        ld(&e, RAX, STKTOP, -VAL_SZ, 1);      /* result words */
        ld(&e, RCX, STKTOP, -VAL_SZ + 8, 1);
        st(&e, RAX, SLOTS, 0, 1);             /* slots[0] = result */
        st(&e, RCX, SLOTS, 8, 1);
        e_lea(&e, R10, SLOTS, VAL_SZ);        /* vm.stackTop = slots + 1 */
        movabs(&e, ADDR, (U64)(uintptr_t)&vm.stackTop);
        st(&e, R10, ADDR, 0, 1);
        mov_imm32(&e, RAX, 1);                /* return true */
        emit_restore_and_ret(&e);
        break;
      default: {
        BinOp b = classify(op);
        if (b.ok) emit_binary(&e, b, &errSites);
        else eligible = false; /* unsupported opcode: interpreter fallback */
        break;
      }
    }
  }

  void *entry = NULL;
  if (eligible && !e.oom) {
    int errLabel = e.count;            /* shared error exit: return false */
    e_xor32(&e, RAX);
    emit_restore_and_ret(&e);
    for (int i = 0; i < errSites.n; i++) patch_rel(&e, errSites.v[i], errLabel);
    for (int i = 0; i < jmpAt.n; i++) patch_rel(&e, jmpAt.v[i], nativeAt[jmpTo.v[i]]);
    if (!e.oom) entry = jit_pool_publish(e.code, (size_t)e.count);
  }

  free(e.code);
  free(nativeAt);
  free(errSites.v);
  free(jmpAt.v);
  free(jmpTo.v);
  return entry;
}

/* ---- startup self-test ------------------------------------------------ */
static I64 selftest_helper(I64 a, I64 b) { return a * b + 7; }
typedef I64 (*Fn1)(I64);
typedef I64 (*Fn2)(I64, I64);

static void *build(Emit *e) {
  if (e->oom) { free(e->code); return NULL; }
  void *fn = jit_pool_publish(e->code, (size_t)e->count);
  free(e->code);
  return fn;
}

static bool selftest(void) {
  { /* f(x) = x + 41  (prologue/epilogue, add_imm, arg in rdi, return rax) */
    Emit e = {0};
    e_push(&e, RBP);
    mov_rr(&e, RAX, RDI);
    add_imm8(&e, RAX, 41);
    e_pop(&e, RBP);
    e_ret(&e);
    Fn1 f = (Fn1)build(&e);
    if (!f || f(2) != 43) return false;
  }
  { /* g(a,b) = a % b  via cqo/idiv (g(17,5)==2, g(40,6)==4) */
    Emit e = {0};
    e_push(&e, RBP);
    mov_rr(&e, RAX, RDI);   /* rax = a */
    mov_rr(&e, RCX, RSI);   /* rcx = b */
    e_cqo(&e);
    e_idiv(&e, RCX);        /* rdx = a % b */
    mov_rr(&e, RAX, RDX);
    e_pop(&e, RBP);
    e_ret(&e);
    Fn2 g = (Fn2)build(&e);
    if (!g || g(17, 5) != 2 || g(40, 6) != 4) return false;
  }
  { /* h(a,b) = a*b - b  (h(6,7)==35) */
    Emit e = {0};
    e_push(&e, RBP);
    mov_rr(&e, RAX, RDI);
    e_imul(&e, RAX, RSI);
    e_sub(&e, RAX, RSI);
    e_pop(&e, RBP);
    e_ret(&e);
    Fn2 h = (Fn2)build(&e);
    if (!h || h(6, 7) != 35) return false;
  }
  { /* lt(a,b) = (a < b) ? 1 : 0  (cmp + setcc + movzx) */
    Emit e = {0};
    e_push(&e, RBP);
    mov_rr(&e, RAX, RDI);
    e_cmp(&e, RAX, RSI);
    e_setcc(&e, CC_L, RAX);
    e_movzx8(&e, RAX, RAX);
    e_pop(&e, RBP);
    e_ret(&e);
    Fn2 lt = (Fn2)build(&e);
    if (!lt || lt(3, 5) != 1 || lt(5, 3) != 0 || lt(4, 4) != 0) return false;
  }
  { /* branch: b(x) = (x==0) ? 100 : 200  (test + jcc + jmp backpatch) */
    Emit e = {0};
    e_push(&e, RBP);
    e_test(&e, RDI, RDI);
    int jne = e_jcc(&e, CC_NE);
    movabs(&e, RAX, 100);
    int jmp = e_jmp(&e);
    int elseL = e.count;
    patch_rel(&e, jne, elseL);
    movabs(&e, RAX, 200);
    patch_rel(&e, jmp, e.count);
    e_pop(&e, RBP);
    e_ret(&e);
    Fn1 b = (Fn1)build(&e);
    if (!b || b(0) != 100 || b(7) != 200) return false;
  }
  { /* call a C helper: c(a,b) = selftest_helper(a,b) = a*b+7 */
    Emit e = {0};
    e_push(&e, RBP);            /* keep rsp 16-aligned across the call */
    movabs(&e, ADDR, (U64)(uintptr_t)&selftest_helper);
    e_call(&e, ADDR);
    e_pop(&e, RBP);
    e_ret(&e);
    Fn2 cc = (Fn2)build(&e);
    if (!cc || cc(6, 7) != 49) return false;
  }
  return true;
}

/* ---- backend hooks ---------------------------------------------------- */
bool jit_selftest_arch(void) { return selftest(); }
void *jit_compile_arch(ObjFunction *fn) { return compile_chunk(fn); }

#endif /* BK_JIT_ENABLED && __x86_64__ */
