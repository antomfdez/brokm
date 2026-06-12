/* object.h - heap-allocated objects.
 *
 * Every object starts with an Obj header carrying GC metadata (mark, gen) and
 * an intrusive `next` pointer, even though v0.1 has no collector yet. This lets
 * the v0.2 mark-sweep and v0.3 generational GC slot in without changing layouts. */
#ifndef BROKM_OBJECT_H
#define BROKM_OBJECT_H

#include "chunk.h"
#include "common.h"
#include "table.h"
#include "value.h"

typedef enum {
  OBJ_STRING,
  OBJ_FUNCTION,
  OBJ_NATIVE,
  OBJ_ARRAY,
  OBJ_CLASS,
  OBJ_INSTANCE,
  OBJ_MAP
} ObjType;

struct Obj {
  ObjType type;
  U8 mark;          /* GC mark bit (v0.2) */
  U8 gen;           /* generation index (v0.3) */
  struct Obj *next; /* intrusive list: sweep + shutdown cleanup */
};

struct ObjString {
  Obj obj;
  int length;
  char *chars;
  U32 hash;
};

typedef struct {
  Obj obj;
  int arity;
  Chunk chunk;
  ObjString *name;
  /* Baseline JIT (v0.5). callCount drives profile gating; nativeCode is the
   * compiled entry (a JitFn) once hot, or NULL to use the interpreter;
   * jitDisabled marks a function the JIT bailed on so it is never retried. */
  int callCount;
  void *nativeCode;
  bool jitDisabled;
} ObjFunction;

typedef Value (*NativeFn)(int argCount, Value *args);

typedef struct {
  Obj obj;
  NativeFn fn;
  ObjString *name;
} ObjNative;

typedef struct {
  Obj obj;
  ValueArray elements;
} ObjArray;

typedef struct {
  Obj obj;
  ObjString *name;
  int fieldCount;
  ObjString **fields; /* declared field names, in order */
  Table methods;      /* method name -> ObjFunction (receiver bound as `this`) */
} ObjClass;

typedef struct {
  Obj obj;
  ObjClass *klass;
  Table fields; /* field name -> value */
} ObjInstance;

/* A string-keyed hash map exposed to brokm via the Map* natives. Structurally
 * an ObjInstance without a class: an Obj wrapping the same string Table, but
 * with keys supplied at runtime instead of declared field names. */
typedef struct {
  Obj obj;
  Table table;
} ObjMap;

#define OBJ_TYPE(v)    (AS_OBJ(v)->type)
#define IS_STRING(v)   object_is_type(v, OBJ_STRING)
#define IS_FUNCTION(v) object_is_type(v, OBJ_FUNCTION)
#define IS_NATIVE(v)   object_is_type(v, OBJ_NATIVE)
#define IS_ARRAY(v)    object_is_type(v, OBJ_ARRAY)
#define IS_CLASS(v)    object_is_type(v, OBJ_CLASS)
#define IS_INSTANCE(v) object_is_type(v, OBJ_INSTANCE)
#define IS_MAP(v)      object_is_type(v, OBJ_MAP)
#define AS_STRING(v)   ((ObjString *)AS_OBJ(v))
#define AS_CSTRING(v)  (((ObjString *)AS_OBJ(v))->chars)
#define AS_FUNCTION(v) ((ObjFunction *)AS_OBJ(v))
#define AS_NATIVE(v)   ((ObjNative *)AS_OBJ(v))
#define AS_ARRAY(v)    ((ObjArray *)AS_OBJ(v))
#define AS_CLASS(v)    ((ObjClass *)AS_OBJ(v))
#define AS_INSTANCE(v) ((ObjInstance *)AS_OBJ(v))
#define AS_MAP(v)      ((ObjMap *)AS_OBJ(v))

static inline bool object_is_type(Value v, ObjType type) {
  return IS_OBJ(v) && AS_OBJ(v)->type == type;
}

/* string_take assumes ownership of `chars`; string_copy duplicates them. Both
 * intern through the VM string table so equal strings share one object. */
ObjString *string_take(char *chars, int length);
ObjString *string_copy(const char *chars, int length);
ObjFunction *function_new(void);
ObjNative *native_new(NativeFn fn, ObjString *name);
ObjArray *array_new(void);
void array_append(ObjArray *array, Value value); /* applies the GC write barrier */
ObjClass *class_new(ObjString *name, int fieldCount);
ObjInstance *instance_new(ObjClass *klass);
ObjMap *map_new(void);
void map_set(ObjMap *map, ObjString *key, Value value); /* barriers key and value */

void object_print(Value v);
void object_free(Obj *obj);  /* free one object (used by the GC sweep) */
void objects_free_all(void); /* free every object (shutdown) */

#endif /* BROKM_OBJECT_H */
