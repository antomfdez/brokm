/* gc.c - the generational mark-sweep collector. */
#include <stdio.h>
#include <stdlib.h>

#include "gc.h"
#include "object.h"
#include "table.h"
#include "vm.h"

#define GC_HEAP_GROW_FACTOR 2
#define GC_HEAP_MIN (1 << 20)  /* major threshold floor: 1 MiB */

#define GEN_YOUNG 0
#define GEN_OLD 1

/* ---- gray worklist (raw alloc; never re-enters the collector) ---------- */

void mark_object(Obj *object) {
  if (object == NULL || object->mark) return;
  object->mark = 1;
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

/* ---- remembered set (old -> young edges) ------------------------------- */

void gc_write_barrier(Obj *owner, Value value) {
  if (owner == NULL || owner->gen != GEN_OLD) return;
  if (!IS_OBJ(value) || AS_OBJ(value)->gen != GEN_YOUNG) return;

  if (vm.rememberedCapacity < vm.rememberedCount + 1) {
    vm.rememberedCapacity =
        vm.rememberedCapacity < 8 ? 8 : vm.rememberedCapacity * 2;
    vm.rememberedSet = (Obj **)realloc(
        vm.rememberedSet, sizeof(Obj *) * (size_t)vm.rememberedCapacity);
    if (vm.rememberedSet == NULL) {
      fprintf(stderr, "brokm: GC out of memory\n");
      exit(70);
    }
  }
  vm.rememberedSet[vm.rememberedCount++] = owner;
}

/* ---- marking ----------------------------------------------------------- */

static void mark_table(Table *table) {
  for (int i = 0; i < table->capacity; i++) {
    Entry *entry = &table->entries[i];
    if (entry->key != NULL) {
      mark_object((Obj *)entry->key);
      mark_value(entry->value);
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
      break;
  }
}

static void mark_roots(bool major) {
  for (Value *slot = vm.stack; slot < vm.stackTop; slot++) mark_value(*slot);
  for (int i = 0; i < vm.frameCount; i++) {
    mark_object((Obj *)vm.frames[i].function);
  }
  mark_table(&vm.globals);

  /* For a minor collection, old objects are not otherwise scanned, so any old
   * object recorded as pointing into the young generation acts as a root. */
  if (!major) {
    for (int i = 0; i < vm.rememberedCount; i++) mark_object(vm.rememberedSet[i]);
  }
}

static void trace_references(void) {
  while (vm.grayCount > 0) {
    Obj *object = vm.grayStack[--vm.grayCount];
    blacken_object(object);
  }
}

/* Drop weak intern-table entries about to be swept: any unmarked string in a
 * major collection, or any unmarked *young* string in a minor collection. */
static void table_remove_white(Table *table, bool major) {
  for (int i = 0; i < table->capacity; i++) {
    Entry *entry = &table->entries[i];
    if (entry->key == NULL) continue;
    Obj *key = (Obj *)entry->key;
    if (key->mark) continue;
    if (major || key->gen == GEN_YOUNG) table_delete(table, entry->key);
  }
}

/* ---- sweep ------------------------------------------------------------- */

static void sweep(bool major) {
  Obj **link = &vm.objects;
  while (*link != NULL) {
    Obj *object = *link;
    if (object->mark) {
      object->mark = 0;
      if (!major && object->gen == GEN_YOUNG) object->gen = GEN_OLD; /* promote */
      link = &object->next;
    } else if (major || object->gen == GEN_YOUNG) {
      *link = object->next;
      object_free(object);
    } else {
      /* surviving old object during a minor collection: leave it, reset mark */
      object->mark = 0;
      link = &object->next;
    }
  }
}

/* ---- driver ------------------------------------------------------------ */

void collect_garbage(bool major) {
  if (!vm.gcEnabled) return;

#ifdef BK_DEBUG_LOG_GC
  printf("-- gc begin (%s)\n", major ? "major" : "minor");
  size_t before = vm.bytesAllocated;
#endif

  mark_roots(major);
  trace_references();
  table_remove_white(&vm.strings, major);
  sweep(major);

  /* After a minor collection the surviving young objects were promoted, so any
   * recorded old->young edges are now old->old; the set can be cleared. */
  vm.rememberedCount = 0;
  vm.bytesSinceMinor = 0;

  if (major) {
    vm.nextGC = vm.bytesAllocated * GC_HEAP_GROW_FACTOR;
    if (vm.nextGC < GC_HEAP_MIN) vm.nextGC = GC_HEAP_MIN;
  }

#ifdef BK_DEBUG_LOG_GC
  printf("-- gc end   (%s: collected %zu bytes, %zu live, nextGC %zu)\n",
         major ? "major" : "minor", before - vm.bytesAllocated,
         vm.bytesAllocated, vm.nextGC);
#endif
}
