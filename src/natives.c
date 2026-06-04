/* natives.c - the Print formatter and native registration. */
#include <stdio.h>
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

void natives_register(void) {
  vm_define_native("Print", native_print);
  vm_define_native("GcCollect", native_gc_collect);
  vm_define_native("GcMinor", native_gc_minor);
}
