/* jit.c - architecture-independent JIT driver: the profiling threshold, the
 * executable-page pool, and the init/shutdown/compile dispatch shared by the
 * arm64 and x86-64 backends. The encoders and bytecode walkers live in
 * jit_arm64.c and jit_x64.c (selected by the target arch); they plug in through
 * jit_compile_arch / jit_selftest_arch and allocate code via jit_pool_publish. */
#include <limits.h>
#include <stddef.h>
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

#ifdef BK_JIT_ENABLED

#include <libkern/OSCacheControl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#include "value.h"

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
  size_t size = 1u << 16; /* 64 KiB */
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

/* Copy a finished routine into an executable page and return its entry. */
void *jit_pool_publish(const void *code, size_t bytes) {
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
  r->used += (bytes + 15u) & ~(size_t)15u; /* keep 16-byte alignment */
  return dst;
}

void jit_pool_free(void) {
  for (Region *r = g_regions; r != NULL;) {
    Region *next = r->next;
    munmap(r->base, r->size);
    free(r);
    r = next;
  }
  g_regions = NULL;
}

/* ---- driver ----------------------------------------------------------- */
void jit_init(void) {
  /* The backends read Values straight out of memory, so the layout must match
   * the constants they bake in (type tag at offset 0, payload at offset 8). */
  bool layoutOK = sizeof(Value) == 16 && offsetof(Value, type) == 0 &&
                  offsetof(Value, as) == 8 && VAL_INT == 2 && VAL_BOOL == 1 &&
                  VAL_NIL == 0;
  g_jitUsable = layoutOK && jit_selftest_arch();
  if (!g_jitUsable) {
    fprintf(stderr, "brokm: JIT unavailable (self-test/layout); interpreter only.\n");
  }
}

void jit_shutdown(void) { jit_pool_free(); }

void jit_try_compile(ObjFunction *fn) {
  void *entry = g_jitUsable ? jit_compile_arch(fn) : NULL;
  if (entry != NULL) fn->nativeCode = entry;
  else fn->jitDisabled = true; /* ineligible/failed: never retried */
  if (getenv("BROKM_JIT_LOG")) {
    fprintf(stderr, "[jit] %s %s\n", fn->name ? fn->name->chars : "<script>",
            entry ? "compiled" : "bailed");
  }
}

#else /* no backend on this platform: every function uses the interpreter */

void jit_init(void) {}
void jit_shutdown(void) {}
void jit_try_compile(ObjFunction *fn) { fn->jitDisabled = true; }

#endif
