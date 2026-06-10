/* api.c - implementation of the public brokm.h embedding API.
 *
 * Instance-based since v0.11: BrokmVM is the internal VM, and every entry
 * point makes its target the current instance for the duration of the call,
 * restoring the previous one on exit — so a host native running on one VM may
 * drive another VM and return cleanly. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "brokm.h"
#include "object.h"
#include "typecheck.h"
#include "vm.h"

#define BROKM_VERSION "1.0.0"

/* BrokmValue and the internal Value are layout-identical 16-byte structs; this
 * union reinterprets between them (the host only ever builds a BrokmValue via
 * the constructors below, so the bits are always a valid Value). */
typedef union {
  BrokmValue pub;
  Value v;
} ValueBridge;

static Value to_value(BrokmValue b) {
  ValueBridge u;
  u.pub = b;
  return u.v;
}
static BrokmValue to_brokm(Value v) {
  ValueBridge u;
  u.v = v;
  return u.pub;
}

/* Make `h` the current instance; pair with leave_vm(prev). */
static VM *enter_vm(BrokmVM *h) {
  VM *prev = vm;
  vm = (VM *)h;
  return prev;
}
static void leave_vm(VM *prev) { vm = prev; }

BrokmVM *brokm_new(void) {
  /* The value bridge and the host-native function-pointer cast both rely on
   * the two value representations matching in size. */
  assert(sizeof(BrokmValue) == sizeof(Value));
  VM *prev = vm;
  VM *created = vm_new(); /* makes itself current */
  vm = prev;
  return (BrokmVM *)created;
}

void brokm_free(BrokmVM *h) { vm_destroy((VM *)h); }

BrokmVM *brokm_current(void) { return (BrokmVM *)vm; }

const char *brokm_version(void) { return BROKM_VERSION; }

static BrokmResult map_result(InterpretResult result) {
  switch (result) {
    case BK_OK: return BROKM_OK;
    case BK_COMPILE_ERROR: return BROKM_COMPILE_ERROR;
    default: return BROKM_RUNTIME_ERROR;
  }
}

BrokmResult brokm_eval(BrokmVM *h, const char *source) {
  VM *prev = enter_vm(h);
  BrokmResult r = map_result(vm_interpret(source));
  vm_api_clear_roots();
  leave_vm(prev);
  return r;
}

static char *read_file(const char *path) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) {
    fprintf(stderr, "brokm: could not open '%s'\n", path);
    return NULL;
  }
  fseek(file, 0L, SEEK_END);
  long size = ftell(file);
  rewind(file);

  char *buffer = (char *)malloc((size_t)size + 1);
  if (buffer == NULL) {
    fprintf(stderr, "brokm: not enough memory to read '%s'\n", path);
    fclose(file);
    return NULL;
  }
  size_t read = fread(buffer, sizeof(char), (size_t)size, file);
  buffer[read] = '\0';
  fclose(file);
  return buffer;
}

BrokmResult brokm_run_file(BrokmVM *h, const char *path) {
  char *source = read_file(path);
  if (source == NULL) return BROKM_RUNTIME_ERROR;

  /* Resolve the file's directory so its #includes are relative to it. */
  char dir[1024];
  const char *slash = strrchr(path, '/');
  if (slash == NULL) {
    snprintf(dir, sizeof(dir), ".");
  } else {
    int len = (int)(slash - path);
    if (len == 0) len = 1; /* root path: keep the leading '/' */
    snprintf(dir, sizeof(dir), "%.*s", len, path);
  }

  VM *prev = enter_vm(h);
  BrokmResult result = map_result(vm_interpret_file(source, dir));
  vm_api_clear_roots();
  leave_vm(prev);
  free(source);
  return result;
}

/* ---- value exchange (v0.9) -------------------------------------------- */

BrokmValue brokm_nil(void) { return to_brokm(NIL_VAL); }
BrokmValue brokm_bool(int b) { return to_brokm(BOOL_VAL(b != 0)); }
BrokmValue brokm_int(int64_t i) { return to_brokm(INT_VAL((I64)i)); }
BrokmValue brokm_float(double f) { return to_brokm(FLOAT_VAL((F64)f)); }

BrokmValue brokm_string(BrokmVM *h, const char *chars) {
  VM *prev = enter_vm(h);
  ObjString *s = string_copy(chars, (int)strlen(chars));
  Value v = OBJ_VAL(s);
  vm_api_root(v); /* keep alive until the next public call boundary */
  leave_vm(prev);
  return to_brokm(v);
}

int brokm_is_nil(BrokmValue v) { return IS_NIL(to_value(v)); }
int brokm_is_bool(BrokmValue v) { return IS_BOOL(to_value(v)); }
int brokm_is_int(BrokmValue v) { return IS_INT(to_value(v)); }
int brokm_is_float(BrokmValue v) { return IS_FLOAT(to_value(v)); }
int brokm_is_string(BrokmValue v) { return IS_STRING(to_value(v)); }

int64_t brokm_as_int(BrokmValue v) {
  Value x = to_value(v);
  return IS_INT(x) ? (int64_t)AS_INT(x) : 0;
}
double brokm_as_float(BrokmValue v) {
  Value x = to_value(v);
  return IS_NUMBER(x) ? (double)AS_F64(x) : 0.0;
}
int brokm_as_bool(BrokmValue v) { return value_truthy(to_value(v)) ? 1 : 0; }
const char *brokm_as_cstring(BrokmValue v) {
  Value x = to_value(v);
  return IS_STRING(x) ? AS_CSTRING(x) : NULL;
}

void brokm_register(BrokmVM *h, const char *name, BrokmNativeFn fn) {
  /* BrokmNativeFn and NativeFn have identical ABI (layout-identical 16-byte
   * value type); reinterpret through a union to avoid a function-pointer cast
   * warning. */
  union {
    BrokmNativeFn host;
    NativeFn internal;
  } cast;
  cast.host = fn;
  VM *prev = enter_vm(h);
  vm_define_native(name, cast.internal);
  typecheck_register_native(name); /* so this VM's scripts may call it */
  leave_vm(prev);
}

int brokm_get_global(BrokmVM *h, const char *name, BrokmValue *out) {
  VM *prev = enter_vm(h);
  ObjString *n = string_copy(name, (int)strlen(name));
  Value v;
  int found = table_get(&vm->globals, n, &v) ? 1 : 0;
  leave_vm(prev);
  if (found && out) *out = to_brokm(v);
  return found;
}

void brokm_set_global(BrokmVM *h, const char *name, BrokmValue value) {
  VM *prev = enter_vm(h);
  ObjString *n = string_copy(name, (int)strlen(name));
  vm_push(OBJ_VAL(n)); /* protect across globals-table growth */
  table_set(&vm->globals, n, to_value(value));
  vm_pop();
  leave_vm(prev);
}

BrokmResult brokm_call(BrokmVM *h, const char *name, int argc,
                       const BrokmValue *args, BrokmValue *result) {
  VM *prev = enter_vm(h);
  ObjString *n = string_copy(name, (int)strlen(name));
  Value callee;
  if (!table_get(&vm->globals, n, &callee)) {
    fprintf(stderr, "brokm: no such function '%s'\n", name);
    vm_api_clear_roots();
    leave_vm(prev);
    return BROKM_RUNTIME_ERROR;
  }
  vm_push(callee);
  for (int i = 0; i < argc; i++) vm_push(to_value(args[i]));

  InterpretResult r = vm_invoke(argc);
  Value res = (r == BK_OK) ? vm_pop() : NIL_VAL;
  vm_api_clear_roots(); /* drop the argument temp-roots */
  if (r == BK_OK) {
    vm_api_root(res); /* a returned string stays valid until the next API call */
    if (result) *result = to_brokm(res);
  }
  leave_vm(prev);
  return map_result(r);
}
