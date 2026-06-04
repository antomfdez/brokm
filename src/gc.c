/* gc.c - the mark-sweep collector. */
#include <stdio.h>
#include <stdlib.h>

#include "gc.h"
#include "object.h"
#include "table.h"
#include "vm.h"

#define GC_HEAP_GROW_FACTOR 2
#define GC_HEAP_MIN (1 << 20) /* never shrink the threshold below 1 MiB */

void mark_object(Obj *object) {
  if (object == NULL || object->mark) return;
  object->mark = 1;

  /* The gray stack uses the raw allocator, not reallocate(), so growing it can
   * never re-enter the collector or perturb byte accounting. */
  if (vm.grayCapacity < vm.grayCount + 1) {
    vm.grayCapacity = vm.grayCapacity < 8 ? 8 : vm.grayCapacity * 2;
    vm.grayStack =
        (Obj **)realloc(vm.grayStack, sizeof(Obj *) * (size_t)vm.grayCapacity);
    if (vm.grayStack == NULL) {
      fprintf(stderr, "brokm: GC out of memory\n");
      exit(70);
    }
  }
  vm.grayStack[vm.grayCount++] = object;
}

void mark_value(Value value) {
  if (IS_OBJ(value)) mark_object(AS_OBJ(value));
}

static void mark_table(Table *table) {
  for (int i = 0; i < table->capacity; i++) {
    Entry *entry = &table->entries[i];
    if (entry->key != NULL) {
      mark_object((Obj *)entry->key);
      mark_value(entry->value);
    }
  }
}

/* Remove interned strings that are about to be swept. The string table holds
 * weak references, so its keys are dropped here before sweep frees them. */
static void table_remove_white(Table *table) {
  for (int i = 0; i < table->capacity; i++) {
    Entry *entry = &table->entries[i];
    if (entry->key != NULL && entry->key->obj.mark == 0) {
      table_delete(table, entry->key);
    }
  }
}

static void blacken_object(Obj *object) {
  switch (object->type) {
    case OBJ_FUNCTION: {
      ObjFunction *fn = (ObjFunction *)object;
      mark_object((Obj *)fn->name);
      for (int i = 0; i < fn->chunk.constants.count; i++) {
        mark_value(fn->chunk.constants.values[i]);
      }
      break;
    }
    case OBJ_NATIVE:
      mark_object((Obj *)((ObjNative *)object)->name);
      break;
    case OBJ_STRING:
      break; /* no outgoing references */
  }
}

static void mark_roots(void) {
  for (Value *slot = vm.stack; slot < vm.stackTop; slot++) mark_value(*slot);
  for (int i = 0; i < vm.frameCount; i++) {
    mark_object((Obj *)vm.frames[i].function);
  }
  mark_table(&vm.globals);
}

static void trace_references(void) {
  while (vm.grayCount > 0) {
    Obj *object = vm.grayStack[--vm.grayCount];
    blacken_object(object);
  }
}

static void sweep(void) {
  Obj **link = &vm.objects;
  while (*link != NULL) {
    Obj *object = *link;
    if (object->mark) {
      object->mark = 0; /* reset for the next cycle */
      link = &object->next;
    } else {
      *link = object->next;
      object_free(object);
    }
  }
}

void collect_garbage(void) {
  if (!vm.gcEnabled) return;

#ifdef BK_DEBUG_LOG_GC
  printf("-- gc begin\n");
  size_t before = vm.bytesAllocated;
#endif

  mark_roots();
  trace_references();
  table_remove_white(&vm.strings);
  sweep();

  vm.nextGC = vm.bytesAllocated * GC_HEAP_GROW_FACTOR;
  if (vm.nextGC < GC_HEAP_MIN) vm.nextGC = GC_HEAP_MIN;

#ifdef BK_DEBUG_LOG_GC
  printf("-- gc end (collected %zu bytes, %zu live, next at %zu)\n",
         before - vm.bytesAllocated, vm.bytesAllocated, vm.nextGC);
#endif
}
