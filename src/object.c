/* object.c - heap object construction, string interning, and teardown. */
#include <stdio.h>
#include <string.h>

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
  table_set(&vm.strings, string, NIL_VAL);
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

static void print_function(ObjFunction *function) {
  if (function->name == NULL) {
    printf("<script>");
    return;
  }
  printf("<func %s>", function->name->chars);
}

void object_print(Value v) {
  switch (OBJ_TYPE(v)) {
    case OBJ_STRING:   printf("%s", AS_CSTRING(v)); break;
    case OBJ_FUNCTION: print_function(AS_FUNCTION(v)); break;
    case OBJ_NATIVE:   printf("<native %s>", AS_NATIVE(v)->name->chars); break;
    default:           printf("<obj>"); break;
  }
}

static void free_object(Obj *obj) {
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
  }
}

/* Walk the intrusive list and release everything. Until the GC lands (v0.2),
 * this is how brokm guarantees no leaks at exit. */
void objects_free_all(void) {
  Obj *obj = vm.objects;
  while (obj != NULL) {
    Obj *next = obj->next;
    free_object(obj);
    obj = next;
  }
  vm.objects = NULL;
}
