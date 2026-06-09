/* jit.h - baseline JIT (v0.5): compile hot functions to native code.
 *
 * The JIT is profile-gated and falls back to the bytecode interpreter for any
 * function it cannot (or chooses not to) compile. Code generators exist for
 * arm64 and x86-64 on macOS and Linux; on other platforms (or with
 * -DBK_NO_JIT) the entry points are no-ops and every function runs on the
 * interpreter. */
#ifndef BROKM_JIT_H
#define BROKM_JIT_H

#include "common.h"
#include "object.h"
#include "value.h"

/* Native entry for a compiled function. `slots` is the call frame base: the
 * callee Value sits at slots[0] and arguments follow, exactly like
 * CallFrame.slots. Returns false on a runtime error (already reported). */
typedef bool (*JitFn)(Value *slots);

#if (defined(__aarch64__) || defined(__x86_64__)) && \
    (defined(__APPLE__) || defined(__linux__)) && !defined(BK_NO_JIT)
#define BK_JIT_ENABLED 1
#endif

void jit_init(void);
void jit_shutdown(void);

#ifdef BK_JIT_ENABLED
/* Shared executable-page pool (jit.c), used by the per-arch backends. */
void *jit_pool_publish(const void *code, size_t bytes); /* copy to an exec page */
void jit_pool_free(void);

/* Per-arch backend hooks (jit_arm64.c / jit_x64.c). jit_compile_arch returns the
 * native entry for fn, or NULL if it bailed; jit_selftest_arch validates the
 * instruction encoders at startup. */
void *jit_compile_arch(ObjFunction *fn);
bool jit_selftest_arch(void);
#endif

/* Attempt to compile `fn` to native code. On success sets fn->nativeCode; on
 * failure (ineligible opcode, platform without a backend, etc.) sets
 * fn->jitDisabled so the attempt is never repeated. */
void jit_try_compile(ObjFunction *fn);

/* Call-count threshold at which a function is compiled (env BROKM_JIT_THRESHOLD,
 * default 50). Returns INT_MAX when the JIT is unavailable. */
int jit_threshold(void);

/* C call-backs the generated code branches into for non-inlined operations
 * (defined in vm.c). Each operates on the VM value stack and returns false on a
 * runtime error. */
bool jit_h_binary(U8 op);
bool jit_h_equal(int negate);
bool jit_h_get_global(ObjString *name);
bool jit_h_set_global(ObjString *name);
bool jit_h_call(int argCount);
bool jit_h_print(int argCount);

#endif /* BROKM_JIT_H */
