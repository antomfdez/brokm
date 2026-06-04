/* brokm.h - public embedding API for the brokm language.
 *
 * Minimal by design: initialize, evaluate source or a file, shut down. The
 * value/object internals are intentionally not exposed yet; a richer C API for
 * registering native functions and exchanging values arrives in a later
 * milestone (see docs/ROADMAP.md). */
#ifndef BROKM_PUBLIC_H
#define BROKM_PUBLIC_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  BROKM_OK = 0,
  BROKM_COMPILE_ERROR = 65,
  BROKM_RUNTIME_ERROR = 70
} BrokmResult;

/* Set up the (single, process-global) brokm runtime. Call once before eval. */
void brokm_init(void);

/* Tear down the runtime and free all heap objects. */
void brokm_shutdown(void);

/* Compile and execute a source string. */
BrokmResult brokm_eval(const char *source);

/* Read, compile, and execute a .bk file. */
BrokmResult brokm_run_file(const char *path);

/* Library version string, e.g. "0.1.0". */
const char *brokm_version(void);

#ifdef __cplusplus
}
#endif

#endif /* BROKM_PUBLIC_H */
