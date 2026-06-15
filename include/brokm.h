/* brokm.h - public embedding API for the brokm language.
 *
 * Lifecycle (v0.11, instance-based): create a VM with brokm_new(), evaluate
 * source or files on it, destroy it with brokm_free(). A process may hold any
 * number of independent VMs — each has its own heap, globals, interned
 * strings, and garbage collector — created, used, and destroyed in any order.
 * The runtime is single-threaded: VMs may be interleaved freely from one
 * thread, but two VMs must not run concurrently from different threads.
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

/* An independent brokm runtime instance (opaque). */
typedef struct BrokmVM BrokmVM;

/* Create a fresh VM. Returns NULL on allocation failure. */
BrokmVM *brokm_new(void);

/* Destroy a VM and free every object it owns. Values obtained from it
 * (e.g. strings from brokm_as_cstring) are invalid afterwards. */
void brokm_free(BrokmVM *vm);

/* Compile and execute a source string on this VM. */
BrokmResult brokm_eval(BrokmVM *vm, const char *source);

/* Read, compile, and execute a .bk file on this VM. */
BrokmResult brokm_run_file(BrokmVM *vm, const char *path);

/* Library version string, e.g. "1.1.0". */
const char *brokm_version(void);

/* ---- value exchange (v0.9) ----------------------------------------------
 *
 * BrokmValue is an opaque, by-value handle the same size as an internal brokm
 * value. Construct values with the brokm_* constructors and inspect them with
 * the predicates/accessors. A BrokmValue holding a string is owned by the VM
 * that created it and remains valid until the next brokm_* call on that VM
 * returns. */
typedef struct {
  uint64_t _bits[2];
} BrokmValue;

BrokmValue brokm_nil(void);
BrokmValue brokm_bool(int b);
BrokmValue brokm_int(int64_t i);
BrokmValue brokm_float(double f);
/* Copied into the VM's heap; GC-managed by that VM. */
BrokmValue brokm_string(BrokmVM *vm, const char *chars);

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
 * a BrokmValue (use brokm_nil() for "no result"). Inside the call,
 * brokm_current() is the VM that invoked it — pass that to brokm_string &c. */
typedef BrokmValue (*BrokmNativeFn)(int argc, const BrokmValue *args);

/* The VM currently executing (the caller, inside a host native); NULL if no
 * brokm code is running. */
BrokmVM *brokm_current(void);

/* Expose `fn` to scripts on this VM under `name` (e.g. a global "HostAdd").
 * Registration is per VM: other instances do not see it. */
void brokm_register(BrokmVM *vm, const char *name, BrokmNativeFn fn);

/* Read a global by name into *out; returns 1 if found, 0 otherwise. */
int brokm_get_global(BrokmVM *vm, const char *name, BrokmValue *out);

/* Define or overwrite a global. */
void brokm_set_global(BrokmVM *vm, const char *name, BrokmValue value);

/* Call a brokm global function by name with argc arguments. On BROKM_OK, the
 * return value is written to *result (may be NULL to ignore). */
BrokmResult brokm_call(BrokmVM *vm, const char *name, int argc,
                       const BrokmValue *args, BrokmValue *result);

#ifdef __cplusplus
}
#endif

#endif /* BROKM_PUBLIC_H */
