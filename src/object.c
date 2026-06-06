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
  function->callCount = 0;
  function->nativeCode = NULL;
  function->jitDisabled = false;
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

ObjClass *class_new(ObjString *name, int fieldCount) {
  ObjClass *klass = (ObjClass *)allocate_object(sizeof(ObjClass), OBJ_CLASS);
  klass->name = name;
  klass->fieldCount = fieldCount;
  klass->fields = fieldCount > 0 ? ALLOCATE(ObjString *, fieldCount) : NULL;
  for (int i = 0; i < fieldCount; i++) klass->fields[i] = NULL;
  table_init(&klass->methods);
  return klass;
}

ObjInstance *instance_new(ObjClass *klass) {
  ObjInstance *instance =
      (ObjInstance *)allocate_object(sizeof(ObjInstance), OBJ_INSTANCE);
  instance->klass = klass;
  table_init(&instance->fields);
  return instance;
}

ObjMap *map_new(void) {
  ObjMap *map = (ObjMap *)allocate_object(sizeof(ObjMap), OBJ_MAP);
  table_init(&map->table);
  return map;
}

void map_set(ObjMap *map, ObjString *key, Value value) {
  table_set(&map->table, key, value);
  /* The store may create an old-map -> young-value edge. */
  gc_write_barrier((Obj *)map, value);
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
    case OBJ_CLASS:    printf("<class %s>", AS_CLASS(v)->name->chars); break;
    case OBJ_INSTANCE:
      printf("<%s instance>", AS_INSTANCE(v)->klass->name->chars);
      break;
    case OBJ_MAP:      printf("<map>"); break;
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
    case OBJ_CLASS: {
      ObjClass *c = (ObjClass *)obj;
      FREE_ARRAY(ObjString *, c->fields, c->fieldCount);
      table_free(&c->methods);
      FREE(ObjClass, obj);
      break;
    }
    case OBJ_INSTANCE: {
      ObjInstance *inst = (ObjInstance *)obj;
      table_free(&inst->fields);
      FREE(ObjInstance, obj);
      break;
    }
    case OBJ_MAP: {
      ObjMap *map = (ObjMap *)obj;
      table_free(&map->table);
      FREE(ObjMap, obj);
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
