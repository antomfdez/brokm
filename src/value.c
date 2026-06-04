/* value.c - Value helpers: equality, truthiness, printing, constant arrays. */
#include <stdio.h>

#include "memory.h"
#include "object.h"
#include "value.h"

void value_array_init(ValueArray *array) {
  array->count = 0;
  array->capacity = 0;
  array->values = NULL;
}

void value_array_write(ValueArray *array, Value value) {
  if (array->capacity < array->count + 1) {
    int oldCap = array->capacity;
    array->capacity = GROW_CAPACITY(oldCap);
    array->values = GROW_ARRAY(Value, array->values, oldCap, array->capacity);
  }
  array->values[array->count] = value;
  array->count++;
}

void value_array_free(ValueArray *array) {
  FREE_ARRAY(Value, array->values, array->capacity);
  value_array_init(array);
}

bool value_truthy(Value v) {
  switch (v.type) {
    case VAL_NIL:   return false;
    case VAL_BOOL:  return AS_BOOL(v);
    case VAL_INT:   return AS_INT(v) != 0;
    case VAL_FLOAT: return AS_FLOAT(v) != 0.0;
    case VAL_OBJ:   return true;
    default:        return false;
  }
}

bool value_equal(Value a, Value b) {
  if (IS_NUMBER(a) && IS_NUMBER(b)) {
    if (IS_INT(a) && IS_INT(b)) return AS_INT(a) == AS_INT(b);
    return AS_F64(a) == AS_F64(b);
  }
  if (a.type != b.type) return false;
  switch (a.type) {
    case VAL_NIL:  return true;
    case VAL_BOOL: return AS_BOOL(a) == AS_BOOL(b);
    case VAL_OBJ:  return AS_OBJ(a) == AS_OBJ(b); /* strings are interned */
    default:       return false;
  }
}

void value_print(Value v) {
  switch (v.type) {
    case VAL_NIL:   printf("nil"); break;
    case VAL_BOOL:  printf(AS_BOOL(v) ? "TRUE" : "FALSE"); break;
    case VAL_INT:   printf("%lld", (long long)AS_INT(v)); break;
    case VAL_FLOAT: printf("%g", AS_FLOAT(v)); break;
    case VAL_OBJ:   object_print(v); break;
    default:        printf("<unknown>"); break;
  }
}
