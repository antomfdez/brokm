/* object.c - heap object construction, string interning, and teardown. */
#include <stdio.h>
#include <string.h>

#include "gc.h"
#include "memory.h"
#include "object.h"
#include "table.h"
#include "vm.h"

static Obj *allocate_object(size_t size, ObjType type) {
  Obj *obj = (Obj *)reallocate(NULL, 0, size);
  obj->type = type;
  obj->mark = 0;
  obj->gen = 0;
  obj->next = vm.objects;
  vm.objects = obj;
  return obj;
}

/* FNV-1a, the same hash clox uses - fast and adequate for interning. */
static U32 hash_string(const char *key, int length) {
  U32 hash = 2166136261u;
  for (int i = 0; i < length; i++) {
    hash ^= (U8)key[i];
    hash *= 16777619u;
  }
  return hash;
}

static ObjString *allocate_string(char *chars, int length, U32 hash) {
  ObjString *string = (ObjString *)allocate_object(sizeof(ObjString), OBJ_STRING);
  string->length = length;
  string->chars = chars;
  string->hash = hash;
  /* table_set may grow the table and trigger a collection; keep the new string
   * reachable on the stack across that window. */
  vm_push(OBJ_VAL(string));
  table_set(&vm.strings, string, NIL_VAL);
  vm_pop();
  return string;
}

ObjString *string_take(char *chars, int length) {
  U32 hash = hash_string(chars, length);
  ObjString *interned = table_find_string(&vm.strings, chars, length, hash);
  if (interned != NULL) {
    FREE_ARRAY(char, chars, length + 1);
    return interned;
  }
  return allocate_string(chars, length, hash);
}

ObjString *string_copy(const char *chars, int length) {
  U32 hash = hash_string(chars, length);
  ObjString *interned = table_find_string(&vm.strings, chars, length, hash);
  if (interned != NULL) return interned;

  char *heap = ALLOCATE(char, length + 1);
  memcpy(heap, chars, length);
  heap[length] = '\0';
  return allocate_string(heap, length, hash);
}

ObjFunction *function_new(void) {
  ObjFunction *function =
      (ObjFunction *)allocate_object(sizeof(ObjFunction), OBJ_FUNCTION);
  function->arity = 0;
  function->name = NULL;
  chunk_init(&function->chunk);
  return function;
}

ObjNative *native_new(NativeFn fn, ObjString *name) {
  ObjNative *native = (ObjNative *)allocate_object(sizeof(ObjNative), OBJ_NATIVE);
  native->fn = fn;
  native->name = name;
  return native;
}

ObjArray *array_new(void) {
  ObjArray *array = (ObjArray *)allocate_object(sizeof(ObjArray), OBJ_ARRAY);
  value_array_init(&array->elements);
  return array;
}

void array_append(ObjArray *array, Value value) {
  value_array_write(&array->elements, value);
  /* The store may create an old-array -> young-value edge. */
  gc_write_barrier((Obj *)array, value);
}

static void print_function(ObjFunction *function) {
  if (function->name == NULL) {
    printf("<script>");
    return;
  }
  printf("<func %s>", function->name->chars);
}

static void print_array(ObjArray *array) {
  printf("[");
  for (int i = 0; i < array->elements.count; i++) {
    if (i > 0) printf(", ");
    value_print(array->elements.values[i]);
  }
  printf("]");
}

void object_print(Value v) {
  switch (OBJ_TYPE(v)) {
    case OBJ_STRING:   printf("%s", AS_CSTRING(v)); break;
    case OBJ_FUNCTION: print_function(AS_FUNCTION(v)); break;
    case OBJ_NATIVE:   printf("<native %s>", AS_NATIVE(v)->name->chars); break;
    case OBJ_ARRAY:    print_array(AS_ARRAY(v)); break;
    default:           printf("<obj>"); break;
  }
}

void object_free(Obj *obj) {
  switch (obj->type) {
    case OBJ_STRING: {
      ObjString *s = (ObjString *)obj;
      FREE_ARRAY(char, s->chars, s->length + 1);
      FREE(ObjString, obj);
      break;
    }
    case OBJ_FUNCTION: {
      ObjFunction *f = (ObjFunction *)obj;
      chunk_free(&f->chunk);
      FREE(ObjFunction, obj);
      break;
    }
    case OBJ_NATIVE:
      FREE(ObjNative, obj);
      break;
    case OBJ_ARRAY: {
      ObjArray *a = (ObjArray *)obj;
      value_array_free(&a->elements);
      FREE(ObjArray, obj);
      break;
    }
  }
}

/* Walk the intrusive list and release everything. Until the GC lands (v0.2),
 * this is how brokm guarantees no leaks at exit. */
void objects_free_all(void) {
  Obj *obj = vm.objects;
  while (obj != NULL) {
    Obj *next = obj->next;
    object_free(obj);
    obj = next;
  }
  vm.objects = NULL;
}
