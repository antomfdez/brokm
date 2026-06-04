/* value.h - the dynamic Value representation.
 *
 * Access goes exclusively through the IS_/AS_/_VAL macros so the underlying
 * representation can later switch to NaN-boxing without touching call sites. */
#ifndef BROKM_VALUE_H
#define BROKM_VALUE_H

#include "common.h"

typedef struct Obj Obj;
typedef struct ObjString ObjString;

typedef enum {
  VAL_NIL,
  VAL_BOOL,
  VAL_INT,
  VAL_FLOAT,
  VAL_OBJ,
  VAL_PTR /* raw manual-memory pointer; invisible to the GC */
} ValueType;

typedef struct {
  ValueType type;
  union {
    bool boolean;
    I64 integer;
    F64 number;
    Obj *obj;
    void *ptr;
  } as;
} Value;

#define NIL_VAL      ((Value){VAL_NIL, {.integer = 0}})
#define BOOL_VAL(b)  ((Value){VAL_BOOL, {.boolean = (b)}})
#define INT_VAL(i)   ((Value){VAL_INT, {.integer = (i)}})
#define FLOAT_VAL(f) ((Value){VAL_FLOAT, {.number = (f)}})
#define OBJ_VAL(o)   ((Value){VAL_OBJ, {.obj = (Obj *)(o)}})
#define PTR_VAL(p)   ((Value){VAL_PTR, {.ptr = (p)}})

#define IS_NIL(v)    ((v).type == VAL_NIL)
#define IS_BOOL(v)   ((v).type == VAL_BOOL)
#define IS_INT(v)    ((v).type == VAL_INT)
#define IS_FLOAT(v)  ((v).type == VAL_FLOAT)
#define IS_OBJ(v)    ((v).type == VAL_OBJ)
#define IS_PTR(v)    ((v).type == VAL_PTR)
#define IS_NUMBER(v) (IS_INT(v) || IS_FLOAT(v))

#define AS_BOOL(v)  ((v).as.boolean)
#define AS_INT(v)   ((v).as.integer)
#define AS_FLOAT(v) ((v).as.number)
#define AS_OBJ(v)   ((v).as.obj)
#define AS_PTR(v)   ((v).as.ptr)

/* Coerce any numeric Value to F64 (for mixed int/float arithmetic). */
#define AS_F64(v) (IS_FLOAT(v) ? AS_FLOAT(v) : (F64)AS_INT(v))

bool value_equal(Value a, Value b);
bool value_truthy(Value v);
void value_print(Value v);

/* Growable array of Values, used for the constant pool. */
typedef struct {
  int count;
  int capacity;
  Value *values;
} ValueArray;

void value_array_init(ValueArray *array);
void value_array_write(ValueArray *array, Value value);
void value_array_free(ValueArray *array);

#endif /* BROKM_VALUE_H */
