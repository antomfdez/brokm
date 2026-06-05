/* jit.c - architecture-independent JIT driver bits: the profiling threshold and
 * the no-backend fallbacks. The arm64/macOS implementation of jit_init,
 * jit_shutdown, and jit_try_compile lives in jit_arm64.c. */
#include <limits.h>
#include <stdlib.h>

#include "jit.h"

int jit_threshold(void) {
#ifdef BK_JIT_ENABLED
  static int cached = -1;
  if (cached < 0) {
    const char *env = getenv("BROKM_JIT_THRESHOLD");
    long t = env ? strtol(env, NULL, 10) : 50;
    if (t < 1) t = 1;
    if (t > INT_MAX) t = INT_MAX;
    cached = (int)t;
  }
  return cached;
#else
  return INT_MAX; /* never hot: the interpreter always runs */
#endif
}

#ifndef BK_JIT_ENABLED
/* No backend on this platform: every function falls back to the interpreter. */
void jit_init(void) {}
void jit_shutdown(void) {}
void jit_try_compile(ObjFunction *fn) { fn->jitDisabled = true; }
#endif
