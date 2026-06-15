/* natives.c - the Print formatter, the standard library, and native
 * registration. */
#ifdef __linux__
#define _DEFAULT_SOURCE /* popen, getline, gettimeofday, nanosleep under -std=c99 */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef BK_FREESTANDING
#include <math.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#endif

#include "gc.h"
#include "natives.h"
#include "object.h"
#include "output.h"
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

#ifndef BK_FREESTANDING
/* Format one conversion into `s`. `flags` holds everything between '%' and the
 * conversion char (width, precision, flags). brokm integers are 64-bit, so
 * signed/unsigned integer conversions inject the `ll` length modifier. Most
 * hosted conversions still run through libc snprintf (into a buffer that grows
 * if a width demands it), then the bytes are emitted to the sink — so output is
 * byte-identical to the old fprintf path. */
#define EMIT_SPEC(TYPE, ARG)                                            \
  do {                                                                  \
    char buf[256];                                                      \
    int need = snprintf(buf, sizeof(buf), fmt, (TYPE)(ARG));            \
    if (need >= 0 && need < (int)sizeof(buf)) {                         \
      bk_sink_cstr(s, buf);                                             \
    } else if (need > 0) {                                              \
      char *big = malloc((size_t)need + 1);                            \
      if (big != NULL) {                                                \
        snprintf(big, (size_t)need + 1, fmt, (TYPE)(ARG));             \
        bk_sink_cstr(s, big);                                          \
        free(big);                                                      \
      }                                                                 \
    }                                                                   \
  } while (0)

static void print_spec(BkSink *s, const char *flags, char conv, Value v) {
  char fmt[48];
  switch (conv) {
    case 'd':
    case 'i':
      snprintf(fmt, sizeof(fmt), "%%%slld", flags);
      EMIT_SPEC(long long, to_i64(v));
      break;
    case 'u':
    case 'o':
    case 'x':
    case 'X':
      snprintf(fmt, sizeof(fmt), "%%%sll%c", flags, conv);
      EMIT_SPEC(unsigned long long, to_i64(v));
      break;
    case 'e':
    case 'E':
    case 'f':
    case 'F':
    case 'g':
    case 'G':
      snprintf(fmt, sizeof(fmt), "%%%s%c", flags, conv);
      EMIT_SPEC(double, to_f64(v));
      break;
    case 'c':
      snprintf(fmt, sizeof(fmt), "%%%sc", flags);
      EMIT_SPEC(int, (int)to_i64(v));
      break;
    case 's':
      snprintf(fmt, sizeof(fmt), "%%%ss", flags);
      EMIT_SPEC(const char *, IS_STRING(v) ? AS_CSTRING(v) : "");
      break;
    case 'p':
      (void)flags;
      bk_sink_cstr(s, "0x");
      if (IS_PTR(v)) {
        bk_sink_u64_base(s, (U64)(uintptr_t)AS_PTR(v), 16, false);
      } else if (IS_OBJ(v)) {
        bk_sink_u64_base(s, (U64)(uintptr_t)AS_OBJ(v), 16, false);
      } else {
        s->emit('0');
      }
      break;
    default:
      s->emit(conv);
      break;
  }
}
#else
static void print_spec(BkSink *s, const char *flags, char conv, Value v) {
  (void)flags; /* Freestanding keeps the small no-snprintf subset for now. */
  switch (conv) {
    case 'd':
    case 'i':
      bk_sink_i64(s, to_i64(v));
      break;
    case 'u':
      bk_sink_u64_base(s, (U64)to_i64(v), 10, false);
      break;
    case 'o':
      bk_sink_u64_base(s, (U64)to_i64(v), 8, false);
      break;
    case 'x':
      bk_sink_u64_base(s, (U64)to_i64(v), 16, false);
      break;
    case 'X':
      bk_sink_u64_base(s, (U64)to_i64(v), 16, true);
      break;
    case 'e':
    case 'E':
    case 'f':
    case 'F':
    case 'g':
    case 'G':
      bk_sink_f64(s, to_f64(v));
      break;
    case 'c':
      s->emit((char)to_i64(v));
      break;
    case 's':
      bk_sink_cstr(s, IS_STRING(v) ? AS_CSTRING(v) : "");
      break;
    case 'p':
      bk_sink_cstr(s, "0x");
      if (IS_PTR(v)) {
        bk_sink_u64_base(s, (U64)(uintptr_t)AS_PTR(v), 16, false);
      } else if (IS_OBJ(v)) {
        bk_sink_u64_base(s, (U64)(uintptr_t)AS_OBJ(v), 16, false);
      } else {
        s->emit('0');
      }
      break;
    default:
      s->emit(conv);
      break;
  }
}
#endif

/* printf-style formatting to a byte sink. bk_format() targets bk_stdout (the
 * OP_PRINT / Print() path); PrintErr() targets bk_stderr. */
static Value bk_format_sink(BkSink *s, int argc, Value *args) {
  if (argc == 0) return NIL_VAL;

  if (!IS_STRING(args[0])) {
    for (int i = 0; i < argc; i++) {
      if (i > 0) s->emit(' ');
      bk_sink_value(s, args[i]);
    }
    return NIL_VAL;
  }

  const char *fmt = AS_CSTRING(args[0]);
  int ai = 1;
  for (const char *p = fmt; *p != '\0'; p++) {
    if (*p != '%') {
      s->emit(*p);
      continue;
    }
    p++;
    if (*p == '%') {
      s->emit('%');
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
    print_spec(s, flags, *p, v);
  }
  return NIL_VAL;
}

Value bk_format(int argc, Value *args) {
  return bk_format_sink(&bk_stdout, argc, args);
}

static Value native_print(int argc, Value *args) { return bk_format(argc, args); }
static Value native_print_err(int argc, Value *args) {
  return bk_format_sink(&bk_stderr, argc, args);
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

/* ---- maps -----------------------------------------------------------------
 * A string-keyed hash map (ObjMap wrapping the VM's string Table). Keys are
 * interned strings; values are any value. MapGet of a missing key is NULL. */

static Value native_map_new(int argc, Value *args) {
  (void)argc;
  (void)args;
  return OBJ_VAL(map_new());
}

static Value native_map_set(int argc, Value *args) {
  if (argc < 3 || !IS_MAP(args[0]) || !IS_STRING(args[1])) return NIL_VAL;
  map_set(AS_MAP(args[0]), AS_STRING(args[1]), args[2]);
  return args[0];
}

static Value native_map_get(int argc, Value *args) {
  if (argc < 2 || !IS_MAP(args[0]) || !IS_STRING(args[1])) return NIL_VAL;
  Value out;
  if (table_get(&AS_MAP(args[0])->table, AS_STRING(args[1]), &out)) return out;
  return NIL_VAL;
}

static Value native_map_has(int argc, Value *args) {
  if (argc < 2 || !IS_MAP(args[0]) || !IS_STRING(args[1])) return BOOL_VAL(false);
  Value out;
  return BOOL_VAL(table_get(&AS_MAP(args[0])->table, AS_STRING(args[1]), &out));
}

static Value native_map_delete(int argc, Value *args) {
  if (argc < 2 || !IS_MAP(args[0]) || !IS_STRING(args[1])) return BOOL_VAL(false);
  return BOOL_VAL(table_delete(&AS_MAP(args[0])->table, AS_STRING(args[1])));
}

/* The table's `count` includes tombstones (it drives the load factor), so scan
 * for live entries to report the user-visible size. */
static Value native_map_len(int argc, Value *args) {
  if (argc < 1 || !IS_MAP(args[0])) return INT_VAL(0);
  Table *t = &AS_MAP(args[0])->table;
  I64 live = 0;
  for (int i = 0; i < t->capacity; i++) {
    if (t->entries[i].key != NULL) live++;
  }
  return INT_VAL(live);
}

/* MapKeys(map) - a new array of the map's keys (order unspecified). */
static Value native_map_keys(int argc, Value *args) {
  if (argc < 1 || !IS_MAP(args[0])) return NIL_VAL;
  Table *t = &AS_MAP(args[0])->table;
  ObjArray *keys = array_new();
  /* Root the array: each array_append may allocate and trigger a collection. */
  vm_push(OBJ_VAL(keys));
  for (int i = 0; i < t->capacity; i++) {
    Entry *entry = &t->entries[i];
    if (entry->key != NULL) array_append(keys, OBJ_VAL(entry->key));
  }
  vm_pop();
  return OBJ_VAL(keys);
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
  vm->gcEnabled = false;
  return NIL_VAL;
}

static Value native_gc_enable(int argc, Value *args) {
  (void)argc;
  (void)args;
  vm->gcEnabled = true;
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
    case VAL_INT:   n = bk_i64_to_cstr(buf, sizeof(buf), AS_INT(v)); break;
    case VAL_FLOAT: n = bk_f64_to_cstr(buf, sizeof(buf), AS_FLOAT(v)); break;
    case VAL_BOOL:  return OBJ_VAL(string_copy(AS_BOOL(v) ? "TRUE" : "FALSE", AS_BOOL(v) ? 4 : 5));
    case VAL_NIL:   return OBJ_VAL(string_copy("nil", 3));
    default:        return OBJ_VAL(string_copy("", 0));
  }
  if (n < 0) n = 0;
  if (n >= (int)sizeof(buf)) n = (int)sizeof(buf) - 1;
  return OBJ_VAL(string_copy(buf, n));
}

#ifndef BK_FREESTANDING
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
#endif /* !BK_FREESTANDING */

/* ---- standard library: math ----------------------------------------------
 * Abs/Min/Max preserve int-vs-float; Sqrt/Pow/Floor/Ceil return F64. */

static Value native_abs(int argc, Value *args) {
  if (argc < 1) return INT_VAL(0);
  if (IS_FLOAT(args[0])) {
    F64 f = AS_FLOAT(args[0]);
    return FLOAT_VAL(f < 0.0 ? -f : f);
  }
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

#ifndef BK_FREESTANDING
static Value native_sqrt(int argc, Value *args)  { return FLOAT_VAL(sqrt(argc >= 1 ? to_f64(args[0]) : 0.0)); }
static Value native_pow(int argc, Value *args)   { return FLOAT_VAL(pow(argc >= 1 ? to_f64(args[0]) : 0.0, argc >= 2 ? to_f64(args[1]) : 0.0)); }
static Value native_floor(int argc, Value *args) { return FLOAT_VAL(floor(argc >= 1 ? to_f64(args[0]) : 0.0)); }
static Value native_ceil(int argc, Value *args)  { return FLOAT_VAL(ceil(argc >= 1 ? to_f64(args[0]) : 0.0)); }
#endif /* !BK_FREESTANDING */

/* ---- self-hosting: emit and run the C VM's real bytecode ------------------
 * These let a brokm program act as a back-end for this very VM. Opcode(name)
 * resolves a mnemonic to its integer OpCode (the enum in chunk.h is the single
 * source of truth), so brokm-generated code stays correct if the enum changes.
 * Assemble(code, consts) turns a byte array plus a constant-pool array into a
 * real, callable ObjFunction whose Chunk runs on this VM directly. */
static const struct { const char *name; int op; } OPCODE_TABLE[] = {
    {"CONSTANT", OP_CONSTANT}, {"NIL", OP_NIL}, {"TRUE", OP_TRUE},
    {"FALSE", OP_FALSE}, {"POP", OP_POP}, {"DEFINE_GLOBAL", OP_DEFINE_GLOBAL},
    {"GET_GLOBAL", OP_GET_GLOBAL}, {"SET_GLOBAL", OP_SET_GLOBAL},
    {"GET_LOCAL", OP_GET_LOCAL}, {"SET_LOCAL", OP_SET_LOCAL},
    {"EQUAL", OP_EQUAL}, {"NOT_EQUAL", OP_NOT_EQUAL}, {"GREATER", OP_GREATER},
    {"GREATER_EQUAL", OP_GREATER_EQUAL}, {"LESS", OP_LESS},
    {"LESS_EQUAL", OP_LESS_EQUAL}, {"ADD", OP_ADD}, {"SUB", OP_SUB},
    {"MUL", OP_MUL}, {"DIV", OP_DIV}, {"MOD", OP_MOD}, {"NEGATE", OP_NEGATE},
    {"NOT", OP_NOT}, {"BIT_AND", OP_BIT_AND}, {"BIT_OR", OP_BIT_OR},
    {"BIT_XOR", OP_BIT_XOR}, {"BIT_NOT", OP_BIT_NOT}, {"SHL", OP_SHL},
    {"SHR", OP_SHR}, {"JUMP", OP_JUMP}, {"JUMP_IF_FALSE", OP_JUMP_IF_FALSE},
    {"JUMP_IF_TRUE", OP_JUMP_IF_TRUE}, {"LOOP", OP_LOOP}, {"CALL", OP_CALL},
    {"PRINT", OP_PRINT}, {"ARRAY", OP_ARRAY}, {"INDEX_GET", OP_INDEX_GET},
    {"INDEX_SET", OP_INDEX_SET}, {"GET_FIELD", OP_GET_FIELD},
    {"SET_FIELD", OP_SET_FIELD}, {"INVOKE", OP_INVOKE}, {"RETURN", OP_RETURN},
    {"CONSTANT_W", OP_CONSTANT_W}, {"DEFINE_GLOBAL_W", OP_DEFINE_GLOBAL_W},
    {"GET_GLOBAL_W", OP_GET_GLOBAL_W}, {"SET_GLOBAL_W", OP_SET_GLOBAL_W},
    {"GET_FIELD_W", OP_GET_FIELD_W}, {"SET_FIELD_W", OP_SET_FIELD_W},
    {"INVOKE_W", OP_INVOKE_W},
};

static Value native_opcode(int argc, Value *args) {
  if (argc < 1 || !IS_STRING(args[0])) return INT_VAL(-1);
  const char *name = AS_CSTRING(args[0]);
  int count = (int)(sizeof(OPCODE_TABLE) / sizeof(OPCODE_TABLE[0]));
  for (int i = 0; i < count; i++) {
    if (strcmp(name, OPCODE_TABLE[i].name) == 0) {
      return INT_VAL(OPCODE_TABLE[i].op);
    }
  }
  return INT_VAL(-1);
}

/* Assemble(code, consts [, arity]) -> callable ObjFunction. `code` is an array
 * of bytes (opcodes and single-byte operands); `consts` is the constant pool,
 * indexed positionally to match the array order the brokm codegen built; the
 * optional `arity` (default 0) sets the parameter count for OP_CALL checks, so
 * brokm can assemble functions, not just scripts. The function is marked
 * jitDisabled: dynamically assembled bytecode is run-once and is not worth (nor
 * safe to blindly walk for) JIT compilation. */
static Value native_assemble(int argc, Value *args) {
  if (argc < 2 || !IS_ARRAY(args[0]) || !IS_ARRAY(args[1])) return NIL_VAL;
  ValueArray *code = &AS_ARRAY(args[0])->elements;
  ValueArray *consts = &AS_ARRAY(args[1])->elements;

  /* Refuse what the C compiler refuses instead of silently wrapping: a
   * constant operand is one byte, so a pool past 256 entries means some
   * emitted index already wrapped and would resolve to the wrong constant
   * (the self-hosting step-22 bug class); likewise any code byte outside
   * 0..255 would be truncated by the U8 cast below. */
  if (consts->count > 256) {
    fprintf(stderr,
            "Assemble: too many constants (%d); a chunk's one-byte operand "
            "can address at most 256.\n",
            consts->count);
    return NIL_VAL;
  }
  for (int i = 0; i < code->count; i++) {
    Value b = code->values[i];
    if (!IS_INT(b) || AS_INT(b) < 0 || AS_INT(b) > 255) {
      fprintf(stderr,
              "Assemble: code byte at index %d is not an integer in 0..255.\n",
              i);
      return NIL_VAL;
    }
  }

  ObjFunction *fn = function_new();
  vm_push(OBJ_VAL(fn)); /* root across name interning + chunk growth */
  fn->arity = (argc >= 3 && IS_INT(args[2])) ? (int)AS_INT(args[2]) : 0;
  fn->name = string_copy("<asm>", 5);
  /* string_copy may collect, promoting the rooted fn to the old generation
   * before the fresh (young) name is stored into it — a raw old->young edge
   * the minor GC can only see through the remembered set. Without this
   * barrier the weakly-interned name is swept and fn->name dangles (caught
   * by ASan on Linux; macOS malloc masked it). */
  gc_write_barrier((Obj *)fn, OBJ_VAL(fn->name));
  for (int i = 0; i < consts->count; i++) {
    chunk_add_constant(&fn->chunk, consts->values[i]);
  }
  for (int i = 0; i < code->count; i++) {
    chunk_write(&fn->chunk, (U8)AS_INT(code->values[i]), 0);
  }
  fn->jitDisabled = true;
  vm_pop();
  return OBJ_VAL(fn);
}

/* MakeClass(name, fields) -> a class value (ObjClass) with the given name and
 * declared field names, so brokm can define structs. Construction then goes
 * through the ordinary OP_CALL path (call_value builds an instance with
 * positional fields), and OP_GET_FIELD/OP_SET_FIELD access them. GC is held off
 * across class_new: it allocates the field-name array after the class object,
 * which is not yet rooted, so a collection there could free it. */
static Value native_make_class(int argc, Value *args) {
  if (argc < 2 || !IS_STRING(args[0]) || !IS_ARRAY(args[1])) return NIL_VAL;
  ValueArray *fields = &AS_ARRAY(args[1])->elements;
  bool savedGc = vm->gcEnabled;
  vm->gcEnabled = false;
  ObjClass *klass = class_new(AS_STRING(args[0]), fields->count);
  for (int i = 0; i < fields->count; i++) {
    if (IS_STRING(fields->values[i])) klass->fields[i] = AS_STRING(fields->values[i]);
  }
  vm->gcEnabled = savedGc;
  return OBJ_VAL(klass);
}

/* AddMethod(klass, name, fn) -> the class, after binding `fn` (an ObjFunction)
 * as a method named `name` on the class. Methods are stored in the class's
 * methods table at compile time by the C compiler; this gives brokm a runtime
 * way to attach one to a class it built with MakeClass, so the self-hosting
 * compiler can emit methods invoked via OP_INVOKE (the method sees the receiver
 * as `this` in local slot 0). The store may create an old-class -> young-fn
 * edge, hence the write barrier (mirrors map_set). */
static Value native_add_method(int argc, Value *args) {
  if (argc < 3 || !IS_CLASS(args[0]) || !IS_STRING(args[1]) ||
      !IS_FUNCTION(args[2]))
    return NIL_VAL;
  ObjClass *klass = AS_CLASS(args[0]);
  table_set(&klass->methods, AS_STRING(args[1]), args[2]);
  gc_write_barrier((Obj *)klass, args[2]);
  return args[0];
}

/* ---- bytecode introspection ------------------------------------------------
 * Read-only views of a compiled function, so brokm code can compare two
 * compiled artifacts structurally — the three-stage bootstrap fixpoint check
 * (examples/fixpoint.bk) diffs the bytecode realcc emits for itself against
 * what its self-compiled self emits. */

/* ChunkCode(fn) -> a new array of the function's bytecode bytes, as ints. */
static Value native_chunk_code(int argc, Value *args) {
  if (argc < 1 || !IS_FUNCTION(args[0])) return NIL_VAL;
  Chunk *chunk = &AS_FUNCTION(args[0])->chunk;
  ObjArray *out = array_new();
  /* Root the array: each array_append may allocate and trigger a collection. */
  vm_push(OBJ_VAL(out));
  for (int i = 0; i < chunk->count; i++) {
    array_append(out, INT_VAL(chunk->code[i]));
  }
  vm_pop();
  return OBJ_VAL(out);
}

/* ChunkConsts(fn) -> a new array of the function's constant-pool values, in
 * pool order. Objects (nested functions, classes, strings) are by reference. */
static Value native_chunk_consts(int argc, Value *args) {
  if (argc < 1 || !IS_FUNCTION(args[0])) return NIL_VAL;
  ValueArray *consts = &AS_FUNCTION(args[0])->chunk.constants;
  ObjArray *out = array_new();
  vm_push(OBJ_VAL(out));
  for (int i = 0; i < consts->count; i++) {
    array_append(out, consts->values[i]);
  }
  vm_pop();
  return OBJ_VAL(out);
}

/* ValueDesc(v) -> a short structural description of a value: its kind, plus
 * the identifying shape of a function ("fn:name/arity") or a class
 * ("class:Name(field,...);methods=N"). Scalars and strings describe only their
 * kind — their contents compare with == in brokm directly. */
static Value native_value_desc(int argc, Value *args) {
  if (argc < 1) return NIL_VAL;
  Value v = args[0];
  char buf[512];
  const char *desc = "unknown";
  switch (v.type) {
    case VAL_NIL:   desc = "nil"; break;
    case VAL_BOOL:  desc = "bool"; break;
    case VAL_INT:   desc = "int"; break;
    case VAL_FLOAT: desc = "float"; break;
    case VAL_PTR:   desc = "ptr"; break;
    case VAL_OBJ:
      switch (OBJ_TYPE(v)) {
        case OBJ_STRING: desc = "str"; break;
        case OBJ_ARRAY:  desc = "array"; break;
        case OBJ_MAP:    desc = "map"; break;
        case OBJ_NATIVE:
          snprintf(buf, sizeof buf, "native:%s", AS_NATIVE(v)->name->chars);
          desc = buf;
          break;
        case OBJ_FUNCTION: {
          ObjFunction *fn = AS_FUNCTION(v);
          snprintf(buf, sizeof buf, "fn:%s/%d",
                   fn->name != NULL ? fn->name->chars : "<script>", fn->arity);
          desc = buf;
          break;
        }
        case OBJ_INSTANCE:
          snprintf(buf, sizeof buf, "instance:%s",
                   AS_INSTANCE(v)->klass->name->chars);
          desc = buf;
          break;
        case OBJ_CLASS: {
          ObjClass *klass = AS_CLASS(v);
          int methods = 0; /* live entries; Table.count includes tombstones */
          for (int i = 0; i < klass->methods.capacity; i++) {
            if (klass->methods.entries[i].key != NULL) methods++;
          }
          size_t off = (size_t)snprintf(buf, sizeof buf, "class:%s(",
                                        klass->name->chars);
          for (int i = 0; i < klass->fieldCount && off < sizeof buf; i++) {
            off += (size_t)snprintf(buf + off, sizeof buf - off, "%s%s",
                                    i > 0 ? "," : "", klass->fields[i]->chars);
          }
          if (off < sizeof buf) {
            snprintf(buf + off, sizeof buf - off, ");methods=%d", methods);
          }
          desc = buf;
          break;
        }
      }
      break;
  }
  return OBJ_VAL(string_copy(desc, (int)strlen(desc)));
}

/* ---- standard library: scripting / OS -------------------------------------
 * The primitives that make brokm usable as a shell-script replacement: program
 * arguments, environment, processes, stdin, time. Everything here is a thin,
 * NULL-on-failure wrapper over POSIX (macOS + Linux, the two CI targets). */

/* Script arguments, stashed by the driver before the VM runs (see
 * natives_set_args). Process-wide on purpose: argv outlives every VM. */
static int g_argCount = 0;
static char **g_argValues = NULL;

void natives_set_args(int argc, char **argv) {
  g_argCount = argc;
  g_argValues = argv;
}

#ifndef BK_FREESTANDING
/* Args() - a new array of the script's command-line arguments (the ones after
 * the script path; for an AOT-compiled program, the ones after the program). */
static Value native_args(int argc, Value *args) {
  (void)argc;
  (void)args;
  ObjArray *out = array_new();
  vm_push(OBJ_VAL(out)); /* root: each append may allocate and collect */
  for (int i = 0; i < g_argCount; i++) {
    ObjString *s = string_copy(g_argValues[i], (int)strlen(g_argValues[i]));
    vm_push(OBJ_VAL(s)); /* root the fresh string across the append's growth */
    array_append(out, OBJ_VAL(s));
    vm_pop();
  }
  vm_pop();
  return OBJ_VAL(out);
}

/* Env(name) - the environment variable's value, or nil if unset. */
static Value native_env(int argc, Value *args) {
  if (argc < 1 || !IS_STRING(args[0])) return NIL_VAL;
  const char *v = getenv(AS_CSTRING(args[0]));
  if (v == NULL) return NIL_VAL;
  return OBJ_VAL(string_copy(v, (int)strlen(v)));
}

/* Exit(code) - terminate the process immediately with the given status. */
static Value native_exit(int argc, Value *args) {
  fflush(stdout);
  fflush(stderr);
  exit(argc >= 1 ? (int)to_i64(args[0]) : 0);
}

/* Shell(cmd) - run a shell command; returns its exit status (-1 on failure). */
static Value native_shell(int argc, Value *args) {
  if (argc < 1 || !IS_STRING(args[0])) return INT_VAL(-1);
  fflush(stdout); /* keep brokm output ordered against the child's */
  int rc = system(AS_CSTRING(args[0]));
  if (rc == -1) return INT_VAL(-1);
  return INT_VAL(WIFEXITED(rc) ? WEXITSTATUS(rc) : -1);
}

/* ShellStr(cmd) - run a shell command and return its stdout as a string
 * (including any trailing newline), or nil if the command could not start. */
static Value native_shell_str(int argc, Value *args) {
  if (argc < 1 || !IS_STRING(args[0])) return NIL_VAL;
  fflush(stdout);
  FILE *p = popen(AS_CSTRING(args[0]), "r");
  if (p == NULL) return NIL_VAL;
  size_t cap = 1024, len = 0;
  char *buf = (char *)malloc(cap);
  if (buf == NULL) {
    pclose(p);
    return NIL_VAL;
  }
  size_t rd;
  while ((rd = fread(buf + len, 1, cap - len, p)) > 0) {
    len += rd;
    if (len == cap) {
      cap *= 2;
      char *grown = (char *)realloc(buf, cap);
      if (grown == NULL) {
        free(buf);
        pclose(p);
        return NIL_VAL;
      }
      buf = grown;
    }
  }
  pclose(p);
  Value s = OBJ_VAL(string_copy(buf, (int)len));
  free(buf);
  return s;
}

/* Input() - one line from stdin without the trailing newline, or nil on EOF. */
static Value native_input(int argc, Value *args) {
  (void)argc;
  (void)args;
  char *line = NULL;
  size_t cap = 0;
  ssize_t n = getline(&line, &cap, stdin);
  if (n < 0) {
    free(line);
    return NIL_VAL;
  }
  if (n > 0 && line[n - 1] == '\n') n--;
  Value s = OBJ_VAL(string_copy(line, (int)n));
  free(line);
  return s;
}

/* Time() - seconds since the Unix epoch. */
static Value native_time(int argc, Value *args) {
  (void)argc;
  (void)args;
  return INT_VAL((I64)time(NULL));
}

/* TimeMs() - milliseconds since the Unix epoch (wall clock, for timing). */
static Value native_time_ms(int argc, Value *args) {
  (void)argc;
  (void)args;
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return INT_VAL((I64)tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

/* Sleep(ms) - pause for the given number of milliseconds. */
static Value native_sleep(int argc, Value *args) {
  if (argc < 1 || !IS_INT(args[0]) || AS_INT(args[0]) <= 0) return NIL_VAL;
  I64 ms = AS_INT(args[0]);
  struct timespec ts;
  ts.tv_sec = (time_t)(ms / 1000);
  ts.tv_nsec = (long)(ms % 1000) * 1000000L;
  nanosleep(&ts, NULL);
  return NIL_VAL;
}

/* AppendFile(path, contents) - append a string to a file; true on success. */
static Value native_append_file(int argc, Value *args) {
  if (argc < 2 || !IS_STRING(args[0]) || !IS_STRING(args[1])) return BOOL_VAL(false);
  FILE *f = fopen(AS_CSTRING(args[0]), "ab");
  if (f == NULL) return BOOL_VAL(false);
  ObjString *c = AS_STRING(args[1]);
  size_t w = fwrite(c->chars, 1, (size_t)c->length, f);
  fclose(f);
  return BOOL_VAL(w == (size_t)c->length);
}

/* FileExists(path) - true if the path can be opened for reading. */
static Value native_file_exists(int argc, Value *args) {
  if (argc < 1 || !IS_STRING(args[0])) return BOOL_VAL(false);
  FILE *f = fopen(AS_CSTRING(args[0]), "rb");
  if (f == NULL) return BOOL_VAL(false);
  fclose(f);
  return BOOL_VAL(true);
}
#endif /* !BK_FREESTANDING */

void natives_register(void) {
#define REGISTER_NATIVE(name, fn) vm_define_native(name, fn);
  BK_NATIVE_CORE_LIST(REGISTER_NATIVE)
#ifndef BK_FREESTANDING
  BK_NATIVE_HOSTED_ONLY_LIST(REGISTER_NATIVE)
#endif
#undef REGISTER_NATIVE
}
