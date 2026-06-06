/* gc.c - the generational mark-sweep collector.
 *
 * A minor collection scans only the young generation: mark_object() ignores old
 * objects entirely, and the only way a young object reachable solely through an
 * old one stays alive is the remembered set, populated by gc_write_barrier().
 * This is what makes the barrier load-bearing (see the red/green test). A major
 * collection marks across both generations. */
#include <stdio.h>
#include <stdlib.h>

#include "gc.h"
#include "object.h"
#include "table.h"
#include "vm.h"

#define GC_HEAP_GROW_FACTOR 2
#define GC_HEAP_MIN (1 << 20) /* major threshold floor: 1 MiB */

#define GEN_YOUNG 0
#define GEN_OLD 1

static bool gc_major = false; /* mode of the collection in progress */

/* ---- gray worklist (raw alloc; never re-enters the collector) ---------- */

void mark_object(Obj *object) {
  if (object == NULL || object->mark) return;
  if (!gc_major && object->gen == GEN_OLD) return; /* minor: old gen is opaque */
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
#ifndef BK_NO_WRITE_BARRIER
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
#else
  (void)owner;
  (void)value;
#endif
}

/* ---- marking ----------------------------------------------------------- */

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
    case OBJ_ARRAY: {
      ObjArray *array = (ObjArray *)object;
      for (int i = 0; i < array->elements.count; i++) {
        mark_value(array->elements.values[i]);
      }
      break;
    }
    case OBJ_CLASS: {
      ObjClass *klass = (ObjClass *)object;
      mark_object((Obj *)klass->name);
      for (int i = 0; i < klass->fieldCount; i++) {
        mark_object((Obj *)klass->fields[i]);
      }
      for (int i = 0; i < klass->methods.capacity; i++) {
        Entry *entry = &klass->methods.entries[i];
        if (entry->key != NULL) {
          mark_object((Obj *)entry->key);
          mark_value(entry->value);
        }
      }
      break;
    }
    case OBJ_INSTANCE: {
      ObjInstance *inst = (ObjInstance *)object;
      mark_object((Obj *)inst->klass);
      for (int i = 0; i < inst->fields.capacity; i++) {
        Entry *entry = &inst->fields.entries[i];
        if (entry->key != NULL) {
          mark_object((Obj *)entry->key);
          mark_value(entry->value);
        }
      }
      break;
    }
    case OBJ_MAP: {
      ObjMap *map = (ObjMap *)object;
      for (int i = 0; i < map->table.capacity; i++) {
        Entry *entry = &map->table.entries[i];
        if (entry->key != NULL) {
          mark_object((Obj *)entry->key); /* keys are strong, unlike vm.strings */
          mark_value(entry->value);
        }
      }
      break;
    }
    case OBJ_STRING:
      break;
  }
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

static void mark_roots(void) {
  for (Value *slot = vm.stack; slot < vm.stackTop; slot++) mark_value(*slot);
  for (int i = 0; i < vm.frameCount; i++) {
    mark_object((Obj *)vm.frames[i].function);
  }
  mark_table(&vm.globals);

  /* For a minor collection, old objects are not scanned, so old objects known
   * to hold young references must be scanned for their young children. */
  if (!gc_major) {
    for (int i = 0; i < vm.rememberedCount; i++) {
      blacken_object(vm.rememberedSet[i]);
    }
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
static void table_remove_white(Table *table) {
  for (int i = 0; i < table->capacity; i++) {
    Entry *entry = &table->entries[i];
    if (entry->key == NULL) continue;
    Obj *key = (Obj *)entry->key;
    if (key->mark) continue;
    if (gc_major || key->gen == GEN_YOUNG) table_delete(table, entry->key);
  }
}

/* ---- sweep ------------------------------------------------------------- */

static void sweep(void) {
  Obj **link = &vm.objects;
  while (*link != NULL) {
    Obj *object = *link;
    if (object->mark) {
      object->mark = 0;
      if (!gc_major && object->gen == GEN_YOUNG) object->gen = GEN_OLD; /* promote */
      link = &object->next;
    } else if (gc_major || object->gen == GEN_YOUNG) {
      *link = object->next;
      object_free(object);
    } else {
      /* surviving old object during a minor collection */
      link = &object->next;
    }
  }
}

/* ---- driver ------------------------------------------------------------ */

void collect_garbage(bool major) {
  if (!vm.gcEnabled) return;
  gc_major = major;

#ifdef BK_DEBUG_LOG_GC
  printf("-- gc begin (%s)\n", major ? "major" : "minor");
  size_t before = vm.bytesAllocated;
#endif

  mark_roots();
  trace_references();
  table_remove_white(&vm.strings);
  sweep();

  /* Only a minor collection promotes its young survivors, dissolving the
   * recorded old->young edges into old->old; a major leaves the set intact. */
  if (!major) vm.rememberedCount = 0;
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
