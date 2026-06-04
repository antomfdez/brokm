/* object.h - heap-allocated objects.
 *
 * Every object starts with an Obj header carrying GC metadata (mark, gen) and
 * an intrusive `next` pointer, even though v0.1 has no collector yet. This lets
 * the v0.2 mark-sweep and v0.3 generational GC slot in without changing layouts. */
#ifndef BROKM_OBJECT_H
#define BROKM_OBJECT_H

#include "chunk.h"
#include "common.h"
#include "value.h"

typedef enum {
  OBJ_STRING,
  OBJ_FUNCTION,
  OBJ_NATIVE
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
} ObjFunction;

typedef Value (*NativeFn)(int argCount, Value *args);

typedef struct {
  Obj obj;
  NativeFn fn;
  ObjString *name;
} ObjNative;

#define OBJ_TYPE(v)    (AS_OBJ(v)->type)
#define IS_STRING(v)   object_is_type(v, OBJ_STRING)
#define IS_FUNCTION(v) object_is_type(v, OBJ_FUNCTION)
#define IS_NATIVE(v)   object_is_type(v, OBJ_NATIVE)
#define AS_STRING(v)   ((ObjString *)AS_OBJ(v))
#define AS_CSTRING(v)  (((ObjString *)AS_OBJ(v))->chars)
#define AS_FUNCTION(v) ((ObjFunction *)AS_OBJ(v))
#define AS_NATIVE(v)   ((ObjNative *)AS_OBJ(v))

static inline bool object_is_type(Value v, ObjType type) {
  return IS_OBJ(v) && AS_OBJ(v)->type == type;
}

/* string_take assumes ownership of `chars`; string_copy duplicates them. Both
 * intern through the VM string table so equal strings share one object. */
ObjString *string_take(char *chars, int length);
ObjString *string_copy(const char *chars, int length);
ObjFunction *function_new(void);
ObjNative *native_new(NativeFn fn, ObjString *name);

void object_print(Value v);
void object_free(Obj *obj);  /* free one object (used by the GC sweep) */
void objects_free_all(void); /* free every object (shutdown) */

#endif /* BROKM_OBJECT_H */
