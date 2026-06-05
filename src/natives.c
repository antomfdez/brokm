/* natives.c - the Print formatter, the standard library, and native
 * registration. */
#include <math.h>
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

/* Mirror value_print to an arbitrary stream (for the no-format-string path). */
static void fprint_value(FILE *out, Value v) {
  switch (v.type) {
    case VAL_NIL:   fputs("nil", out); break;
    case VAL_BOOL:  fputs(AS_BOOL(v) ? "TRUE" : "FALSE", out); break;
    case VAL_INT:   fprintf(out, "%lld", (long long)AS_INT(v)); break;
    case VAL_FLOAT: fprintf(out, "%g", AS_FLOAT(v)); break;
    case VAL_OBJ:
      if (IS_STRING(v)) fputs(AS_CSTRING(v), out);
      else value_print(v);
      break;
    case VAL_PTR:   fputs(AS_PTR(v) != NULL ? "<ptr>" : "NULL", out); break;
    default:        fputs("<unknown>", out); break;
  }
}

/* `flags` holds everything between '%' and the conversion char (width,
 * precision, flags). brokm integers are 64-bit, so signed/unsigned integer
 * conversions inject the `ll` length modifier. */
static void print_spec(FILE *out, const char *flags, char conv, Value v) {
  char fmt[48];
  switch (conv) {
    case 'd':
    case 'i':
      snprintf(fmt, sizeof(fmt), "%%%slld", flags);
      fprintf(out, fmt, (long long)to_i64(v));
      break;
    case 'u':
    case 'o':
    case 'x':
    case 'X':
      snprintf(fmt, sizeof(fmt), "%%%sll%c", flags, conv);
      fprintf(out, fmt, (unsigned long long)to_i64(v));
      break;
    case 'e':
    case 'E':
    case 'f':
    case 'F':
    case 'g':
    case 'G':
      snprintf(fmt, sizeof(fmt), "%%%s%c", flags, conv);
      fprintf(out, fmt, to_f64(v));
      break;
    case 'c':
      snprintf(fmt, sizeof(fmt), "%%%sc", flags);
      fprintf(out, fmt, (int)to_i64(v));
      break;
    case 's':
      snprintf(fmt, sizeof(fmt), "%%%ss", flags);
      fprintf(out, fmt, IS_STRING(v) ? AS_CSTRING(v) : "");
      break;
    default:
      fputc(conv, out);
      break;
  }
}

/* printf-style formatting to `out`. bk_format() is the stdout wrapper used by
 * OP_PRINT and Print(); PrintErr() targets stderr. */
static Value bk_format_to(FILE *out, int argc, Value *args) {
  if (argc == 0) return NIL_VAL;

  if (!IS_STRING(args[0])) {
    for (int i = 0; i < argc; i++) {
      if (i > 0) fputc(' ', out);
      fprint_value(out, args[i]);
    }
    return NIL_VAL;
  }

  const char *fmt = AS_CSTRING(args[0]);
  int ai = 1;
  for (const char *p = fmt; *p != '\0'; p++) {
    if (*p != '%') {
      fputc(*p, out);
      continue;
    }
    p++;
    if (*p == '%') {
      fputc('%', out);
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
    print_spec(out, flags, *p, v);
  }
  return NIL_VAL;
}

Value bk_format(int argc, Value *args) { return bk_format_to(stdout, argc, args); }

static Value native_print(int argc, Value *args) { return bk_format(argc, args); }
static Value native_print_err(int argc, Value *args) {
  return bk_format_to(stderr, argc, args);
}

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

/* ---- standard library: strings -------------------------------------------
 * HolyC keeps characters as integers, so CharAt/Chr bridge ints and 1-char
 * strings. Returned strings go through string_copy, which is GC-safe (it
 * protects the new string across interning). */

/* CharAt(s, i) - byte/char code at index i, or -1 if out of range. */
static Value native_char_at(int argc, Value *args) {
  if (argc < 2 || !IS_STRING(args[0]) || !IS_INT(args[1])) return INT_VAL(-1);
  ObjString *s = AS_STRING(args[0]);
  I64 i = AS_INT(args[1]);
  if (i < 0 || i >= s->length) return INT_VAL(-1);
  return INT_VAL((I64)(U8)s->chars[i]);
}

/* Chr(code) - a one-character string for the given char code. */
static Value native_chr(int argc, Value *args) {
  char c = (char)(argc >= 1 && IS_INT(args[0]) ? AS_INT(args[0]) : 0);
  return OBJ_VAL(string_copy(&c, 1));
}

/* Substr(s, start, len) - substring, with start/len clamped to bounds. */
static Value native_substr(int argc, Value *args) {
  if (argc < 3 || !IS_STRING(args[0]) || !IS_INT(args[1]) || !IS_INT(args[2])) {
    return NIL_VAL;
  }
  ObjString *s = AS_STRING(args[0]);
  I64 start = AS_INT(args[1]), len = AS_INT(args[2]);
  if (start < 0) start = 0;
  if (start > s->length) start = s->length;
  if (len < 0) len = 0;
  if (start + len > s->length) len = s->length - start;
  return OBJ_VAL(string_copy(s->chars + start, (int)len));
}

/* IndexOf(s, sub) - first index of sub in s, or -1. */
static Value native_index_of(int argc, Value *args) {
  if (argc < 2 || !IS_STRING(args[0]) || !IS_STRING(args[1])) return INT_VAL(-1);
  const char *hay = AS_CSTRING(args[0]);
  const char *at = strstr(hay, AS_CSTRING(args[1]));
  return INT_VAL(at != NULL ? (I64)(at - hay) : -1);
}

/* ToInt(x) - parse a string (base 10), truncate a float, or pass an int. */
static Value native_to_int(int argc, Value *args) {
  if (argc < 1) return INT_VAL(0);
  if (IS_INT(args[0])) return args[0];
  if (IS_FLOAT(args[0])) return INT_VAL((I64)AS_FLOAT(args[0]));
  if (IS_STRING(args[0])) return INT_VAL((I64)strtoll(AS_CSTRING(args[0]), NULL, 10));
  return INT_VAL(0);
}

/* ToStr(x) - render a value as a string (matches value_print formatting). */
static Value native_to_str(int argc, Value *args) {
  if (argc < 1) return OBJ_VAL(string_copy("", 0));
  Value v = args[0];
  if (IS_STRING(v)) return v;
  char buf[32];
  int n = 0;
  switch (v.type) {
    case VAL_INT:   n = snprintf(buf, sizeof(buf), "%lld", (long long)AS_INT(v)); break;
    case VAL_FLOAT: n = snprintf(buf, sizeof(buf), "%g", AS_FLOAT(v)); break;
    case VAL_BOOL:  return OBJ_VAL(string_copy(AS_BOOL(v) ? "TRUE" : "FALSE", AS_BOOL(v) ? 4 : 5));
    case VAL_NIL:   return OBJ_VAL(string_copy("nil", 3));
    default:        return OBJ_VAL(string_copy("", 0));
  }
  if (n < 0) n = 0;
  if (n >= (int)sizeof(buf)) n = (int)sizeof(buf) - 1;
  return OBJ_VAL(string_copy(buf, n));
}

/* ---- standard library: file I/O ------------------------------------------ */

/* ReadFile(path) - whole file as a string, or nil if it cannot be opened. */
static Value native_read_file(int argc, Value *args) {
  if (argc < 1 || !IS_STRING(args[0])) return NIL_VAL;
  FILE *f = fopen(AS_CSTRING(args[0]), "rb");
  if (f == NULL) return NIL_VAL;
  fseek(f, 0L, SEEK_END);
  long size = ftell(f);
  rewind(f);
  if (size < 0) { fclose(f); return NIL_VAL; }
  char *buf = (char *)malloc((size_t)size + 1);
  if (buf == NULL) { fclose(f); return NIL_VAL; }
  size_t rd = fread(buf, 1, (size_t)size, f);
  fclose(f);
  buf[rd] = '\0';
  Value s = OBJ_VAL(string_copy(buf, (int)rd)); /* copies; GC-safe */
  free(buf);
  return s;
}

/* WriteFile(path, contents) - write a string to a file; true on success. */
static Value native_write_file(int argc, Value *args) {
  if (argc < 2 || !IS_STRING(args[0]) || !IS_STRING(args[1])) return BOOL_VAL(false);
  FILE *f = fopen(AS_CSTRING(args[0]), "wb");
  if (f == NULL) return BOOL_VAL(false);
  ObjString *c = AS_STRING(args[1]);
  size_t w = fwrite(c->chars, 1, (size_t)c->length, f);
  fclose(f);
  return BOOL_VAL(w == (size_t)c->length);
}

/* ---- standard library: math ----------------------------------------------
 * Abs/Min/Max preserve int-vs-float; Sqrt/Pow/Floor/Ceil return F64. */

static Value native_abs(int argc, Value *args) {
  if (argc < 1) return INT_VAL(0);
  if (IS_FLOAT(args[0])) return FLOAT_VAL(fabs(AS_FLOAT(args[0])));
  return INT_VAL((I64)llabs((long long)to_i64(args[0])));
}

static Value native_min(int argc, Value *args) {
  if (argc < 2) return argc >= 1 ? args[0] : NIL_VAL;
  Value a = args[0], b = args[1];
  if (IS_INT(a) && IS_INT(b)) return AS_INT(a) <= AS_INT(b) ? a : b;
  return to_f64(a) <= to_f64(b) ? a : b;
}

static Value native_max(int argc, Value *args) {
  if (argc < 2) return argc >= 1 ? args[0] : NIL_VAL;
  Value a = args[0], b = args[1];
  if (IS_INT(a) && IS_INT(b)) return AS_INT(a) >= AS_INT(b) ? a : b;
  return to_f64(a) >= to_f64(b) ? a : b;
}

static Value native_sqrt(int argc, Value *args)  { return FLOAT_VAL(sqrt(argc >= 1 ? to_f64(args[0]) : 0.0)); }
static Value native_pow(int argc, Value *args)   { return FLOAT_VAL(pow(argc >= 1 ? to_f64(args[0]) : 0.0, argc >= 2 ? to_f64(args[1]) : 0.0)); }
static Value native_floor(int argc, Value *args) { return FLOAT_VAL(floor(argc >= 1 ? to_f64(args[0]) : 0.0)); }
static Value native_ceil(int argc, Value *args)  { return FLOAT_VAL(ceil(argc >= 1 ? to_f64(args[0]) : 0.0)); }

void natives_register(void) {
  vm_define_native("Print", native_print);
  vm_define_native("PrintErr", native_print_err);
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
  /* v0.6 standard library */
  vm_define_native("CharAt", native_char_at);
  vm_define_native("Chr", native_chr);
  vm_define_native("Substr", native_substr);
  vm_define_native("IndexOf", native_index_of);
  vm_define_native("ToInt", native_to_int);
  vm_define_native("ToStr", native_to_str);
  vm_define_native("ReadFile", native_read_file);
  vm_define_native("WriteFile", native_write_file);
  vm_define_native("Abs", native_abs);
  vm_define_native("Min", native_min);
  vm_define_native("Max", native_max);
  vm_define_native("Sqrt", native_sqrt);
  vm_define_native("Pow", native_pow);
  vm_define_native("Floor", native_floor);
  vm_define_native("Ceil", native_ceil);
}
