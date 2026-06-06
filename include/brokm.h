/* brokm.h - public embedding API for the brokm language.
 *
 * Lifecycle: initialize, evaluate source or a file, shut down. The runtime is a
 * single process-global instance (multi-instance VMs are a later milestone).
 *
 * The value-exchange API (v0.9) lets a host program register its own native
 * functions, read and set globals, exchange values, and call brokm functions
 * from C. Only scalars (int/float/bool/nil) and strings cross the boundary in
 * this cut; arrays/instances/maps do not yet. */
#ifndef BROKM_PUBLIC_H
#define BROKM_PUBLIC_H

#include <stdint.h>

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

/* ---- value exchange (v0.9) ----------------------------------------------
 *
 * BrokmValue is an opaque, by-value handle the same size as an internal brokm
 * value. Construct values with the brokm_* constructors and inspect them with
 * the predicates/accessors. A BrokmValue holding a string is owned by the
 * runtime and remains valid until the next brokm_* call returns. */
typedef struct {
  uint64_t _bits[2];
} BrokmValue;

BrokmValue brokm_nil(void);
BrokmValue brokm_bool(int b);
BrokmValue brokm_int(int64_t i);
BrokmValue brokm_float(double f);
BrokmValue brokm_string(const char *chars); /* copied; GC-managed */

int brokm_is_nil(BrokmValue v);
int brokm_is_bool(BrokmValue v);
int brokm_is_int(BrokmValue v);
int brokm_is_float(BrokmValue v);
int brokm_is_string(BrokmValue v);

int64_t brokm_as_int(BrokmValue v);       /* 0 if not an int */
double brokm_as_float(BrokmValue v);      /* int promotes to float; else 0 */
int brokm_as_bool(BrokmValue v);          /* truthiness */
const char *brokm_as_cstring(BrokmValue v); /* NULL if not a string */

/* A host function callable from brokm scripts. Arguments are read-only; return
 * a BrokmValue (use brokm_nil() for "no result"). */
typedef BrokmValue (*BrokmNativeFn)(int argc, const BrokmValue *args);

/* Expose `fn` to brokm scripts under `name` (e.g. a global "HostAdd"). */
void brokm_register(const char *name, BrokmNativeFn fn);

/* Read a global by name into *out; returns 1 if found, 0 otherwise. */
int brokm_get_global(const char *name, BrokmValue *out);

/* Define or overwrite a global. */
void brokm_set_global(const char *name, BrokmValue value);

/* Call a brokm global function by name with argc arguments. On BROKM_OK, the
 * return value is written to *result (may be NULL to ignore). */
BrokmResult brokm_call(const char *name, int argc, const BrokmValue *args,
                       BrokmValue *result);

#ifdef __cplusplus
}
#endif

#endif /* BROKM_PUBLIC_H */
