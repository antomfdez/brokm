/* natives.c - the Print formatter and native registration. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gc.h"
#include "natives.h"
#include "object.h"
#include "vm.h"

#define CONVERSIONS "diouxXeEfFgGcsp"

static I64 to_i64(Value v) {
  if (IS_INT(v)) return AS_INT(v);
  if (IS_FLOAT(v)) return (I64)AS_FLOAT(v);
  if (IS_BOOL(v)) return AS_BOOL(v) ? 1 : 0;
  return 0;
}
static F64 to_f64(Value v) {
  if (IS_NUMBER(v)) return AS_F64(v);
  if (IS_BOOL(v)) return AS_BOOL(v) ? 1.0 : 0.0;
  return 0.0;
}

/* `flags` holds everything between '%' and the conversion char (width,
 * precision, flags). brokm integers are 64-bit, so signed/unsigned integer
 * conversions inject the `ll` length modifier. */
static void print_spec(const char *flags, char conv, Value v) {
  char fmt[48];
  switch (conv) {
    case 'd':
    case 'i':
      snprintf(fmt, sizeof(fmt), "%%%slld", flags);
      printf(fmt, (long long)to_i64(v));
      break;
    case 'u':
    case 'o':
    case 'x':
    case 'X':
      snprintf(fmt, sizeof(fmt), "%%%sll%c", flags, conv);
      printf(fmt, (unsigned long long)to_i64(v));
      break;
    case 'e':
    case 'E':
    case 'f':
    case 'F':
    case 'g':
    case 'G':
      snprintf(fmt, sizeof(fmt), "%%%s%c", flags, conv);
      printf(fmt, to_f64(v));
      break;
    case 'c':
      snprintf(fmt, sizeof(fmt), "%%%sc", flags);
      printf(fmt, (int)to_i64(v));
      break;
    case 's':
      snprintf(fmt, sizeof(fmt), "%%%ss", flags);
      printf(fmt, IS_STRING(v) ? AS_CSTRING(v) : "");
      break;
    default:
      putchar(conv);
      break;
  }
}

Value bk_format(int argc, Value *args) {
  if (argc == 0) return NIL_VAL;

  if (!IS_STRING(args[0])) {
    for (int i = 0; i < argc; i++) {
      if (i > 0) putchar(' ');
      value_print(args[i]);
    }
    return NIL_VAL;
  }

  const char *fmt = AS_CSTRING(args[0]);
  int ai = 1;
  for (const char *p = fmt; *p != '\0'; p++) {
    if (*p != '%') {
      putchar(*p);
      continue;
    }
    p++;
    if (*p == '%') {
      putchar('%');
      continue;
    }
    char flags[32];
    int f = 0;
    while (*p != '\0' && strchr(CONVERSIONS, *p) == NULL && f < 31) {
      flags[f++] = *p++;
    }
    flags[f] = '\0';
    if (*p == '\0') break;
    Value v = (ai < argc) ? args[ai++] : NIL_VAL;
    print_spec(flags, *p, v);
  }
  return NIL_VAL;
}

static Value native_print(int argc, Value *args) { return bk_format(argc, args); }

/* Force a full (major) garbage collection. Returns nil. */
static Value native_gc_collect(int argc, Value *args) {
  (void)argc;
  (void)args;
  collect_garbage(true);
  return NIL_VAL;
}

/* Force a minor (young-generation) collection. Returns nil. */
static Value native_gc_minor(int argc, Value *args) {
  (void)argc;
  (void)args;
  collect_garbage(false);
  return NIL_VAL;
}

/* Len(x) - element count of an array, or character count of a string. */
static Value native_len(int argc, Value *args) {
  if (argc < 1) return INT_VAL(0);
  if (IS_ARRAY(args[0])) return INT_VAL(AS_ARRAY(args[0])->elements.count);
  if (IS_STRING(args[0])) return INT_VAL(AS_STRING(args[0])->length);
  return INT_VAL(0);
}

/* Append(array, value) - grow the array by one element; returns the array. */
static Value native_append(int argc, Value *args) {
  if (argc < 2 || !IS_ARRAY(args[0])) return NIL_VAL;
  array_append(AS_ARRAY(args[0]), args[1]);
  return args[0];
}

/* ---- manual memory --------------------------------------------------------
 * Raw, GC-invisible memory for low-level work. MAlloc returns a VAL_PTR that
 * the collector never tracks or follows; it lives until Free. Access is by
 * typed element index (PeekI64(p, i) reads the i-th 8-byte slot, etc.). These
 * are deliberately unchecked beyond NULL - bounds and lifetime are the caller's
 * responsibility, which is the point of manual memory. */

static Value native_malloc(int argc, Value *args) {
  if (argc < 1 || !IS_INT(args[0]) || AS_INT(args[0]) <= 0) return NIL_VAL;
  size_t n = (size_t)AS_INT(args[0]);
  void *p = malloc(n);
  if (p == NULL) return NIL_VAL; /* a null pointer is represented as nil */
  memset(p, 0, n);
  return PTR_VAL(p);
}

static Value native_free(int argc, Value *args) {
  if (argc >= 1 && IS_PTR(args[0])) free(AS_PTR(args[0]));
  return NIL_VAL;
}

static bool ptr_index(int argc, Value *args, void **base, I64 *index) {
  if (argc < 2 || !IS_PTR(args[0]) || AS_PTR(args[0]) == NULL ||
      !IS_INT(args[1])) {
    return false;
  }
  *base = AS_PTR(args[0]);
  *index = AS_INT(args[1]);
  return true;
}

static Value native_peek_u8(int argc, Value *args) {
  void *base;
  I64 i;
  if (!ptr_index(argc, args, &base, &i)) return NIL_VAL;
  return INT_VAL((I64)((U8 *)base)[i]);
}

static Value native_poke_u8(int argc, Value *args) {
  void *base;
  I64 i;
  if (!ptr_index(argc, args, &base, &i) || argc < 3) return NIL_VAL;
  ((U8 *)base)[i] = (U8)(IS_INT(args[2]) ? AS_INT(args[2]) : 0);
  return args[2];
}

static Value native_peek_i64(int argc, Value *args) {
  void *base;
  I64 i;
  if (!ptr_index(argc, args, &base, &i)) return NIL_VAL;
  return INT_VAL(((I64 *)base)[i]);
}

static Value native_poke_i64(int argc, Value *args) {
  void *base;
  I64 i;
  if (!ptr_index(argc, args, &base, &i) || argc < 3) return NIL_VAL;
  ((I64 *)base)[i] = IS_INT(args[2]) ? AS_INT(args[2]) : 0;
  return args[2];
}

static Value native_peek_f64(int argc, Value *args) {
  void *base;
  I64 i;
  if (!ptr_index(argc, args, &base, &i)) return NIL_VAL;
  return FLOAT_VAL(((F64 *)base)[i]);
}

static Value native_poke_f64(int argc, Value *args) {
  void *base;
  I64 i;
  if (!ptr_index(argc, args, &base, &i) || argc < 3) return NIL_VAL;
  ((F64 *)base)[i] = IS_NUMBER(args[2]) ? AS_F64(args[2]) : 0.0;
  return args[2];
}

static Value native_peek_ptr(int argc, Value *args) {
  void *base;
  I64 i;
  if (!ptr_index(argc, args, &base, &i)) return NIL_VAL;
  void *loaded = ((void **)base)[i];
  return loaded != NULL ? PTR_VAL(loaded) : NIL_VAL; /* null reads back as nil */
}

static Value native_poke_ptr(int argc, Value *args) {
  void *base;
  I64 i;
  if (!ptr_index(argc, args, &base, &i) || argc < 3) return NIL_VAL;
  ((void **)base)[i] = IS_PTR(args[2]) ? AS_PTR(args[2]) : NULL;
  return args[2];
}

/* Bracket a section where the collector must not run (deterministic, no pauses).
 * Allocations during the window are reclaimed once GC is re-enabled. */
static Value native_gc_disable(int argc, Value *args) {
  (void)argc;
  (void)args;
  vm.gcEnabled = false;
  return NIL_VAL;
}

static Value native_gc_enable(int argc, Value *args) {
  (void)argc;
  (void)args;
  vm.gcEnabled = true;
  return NIL_VAL;
}

void natives_register(void) {
  vm_define_native("Print", native_print);
  vm_define_native("GcCollect", native_gc_collect);
  vm_define_native("GcMinor", native_gc_minor);
  vm_define_native("GcDisable", native_gc_disable);
  vm_define_native("GcEnable", native_gc_enable);
  vm_define_native("Len", native_len);
  vm_define_native("Append", native_append);
  vm_define_native("MAlloc", native_malloc);
  vm_define_native("Free", native_free);
  vm_define_native("PeekU8", native_peek_u8);
  vm_define_native("PokeU8", native_poke_u8);
  vm_define_native("PeekI64", native_peek_i64);
  vm_define_native("PokeI64", native_poke_i64);
  vm_define_native("PeekF64", native_peek_f64);
  vm_define_native("PokeF64", native_poke_f64);
  vm_define_native("PeekPtr", native_peek_ptr);
  vm_define_native("PokePtr", native_poke_ptr);
}
