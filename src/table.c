/* table.c - hash table with linear probing and tombstones. */
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"

#define TABLE_MAX_LOAD 0.75

void table_init(Table *table) {
  table->count = 0;
  table->capacity = 0;
  table->entries = NULL;
}

void table_free(Table *table) {
  FREE_ARRAY(Entry, table->entries, table->capacity);
  table_init(table);
}

/* A tombstone is a NULL key with a non-nil (BOOL true) value. */
static Entry *find_entry(Entry *entries, int capacity, ObjString *key) {
  U32 index = key->hash & (U32)(capacity - 1);
  Entry *tombstone = NULL;
  for (;;) {
    Entry *entry = &entries[index];
    if (entry->key == NULL) {
      if (IS_NIL(entry->value)) {
        return tombstone != NULL ? tombstone : entry;
      }
      if (tombstone == NULL) tombstone = entry;
    } else if (entry->key == key) {
      return entry;
    }
    index = (index + 1) & (U32)(capacity - 1);
  }
}

static void adjust_capacity(Table *table, int capacity) {
  Entry *entries = ALLOCATE(Entry, capacity);
  for (int i = 0; i < capacity; i++) {
    entries[i].key = NULL;
    entries[i].value = NIL_VAL;
  }

  table->count = 0;
  for (int i = 0; i < table->capacity; i++) {
    Entry *entry = &table->entries[i];
    if (entry->key == NULL) continue;
    Entry *dest = find_entry(entries, capacity, entry->key);
    dest->key = entry->key;
    dest->value = entry->value;
    table->count++;
  }

  FREE_ARRAY(Entry, table->entries, table->capacity);
  table->entries = entries;
  table->capacity = capacity;
}

bool table_get(Table *table, ObjString *key, Value *out) {
  if (table->count == 0) return false;
  Entry *entry = find_entry(table->entries, table->capacity, key);
  if (entry->key == NULL) return false;
  *out = entry->value;
  return true;
}

bool table_set(Table *table, ObjString *key, Value value) {
  if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
    int capacity = GROW_CAPACITY(table->capacity);
    adjust_capacity(table, capacity);
  }
  Entry *entry = find_entry(table->entries, table->capacity, key);
  bool isNew = entry->key == NULL;
  if (isNew && IS_NIL(entry->value)) table->count++;
  entry->key = key;
  entry->value = value;
  return isNew;
}

bool table_delete(Table *table, ObjString *key) {
  if (table->count == 0) return false;
  Entry *entry = find_entry(table->entries, table->capacity, key);
  if (entry->key == NULL) return false;
  entry->key = NULL;
  entry->value = BOOL_VAL(true); /* tombstone */
  return true;
}

ObjString *table_find_string(Table *table, const char *chars, int length,
                             U32 hash) {
  if (table->count == 0) return NULL;
  U32 index = hash & (U32)(table->capacity - 1);
  for (;;) {
    Entry *entry = &table->entries[index];
    if (entry->key == NULL) {
      if (IS_NIL(entry->value)) return NULL; /* empty, non-tombstone */
    } else if (entry->key->length == length && entry->key->hash == hash &&
               memcmp(entry->key->chars, chars, length) == 0) {
      return entry->key;
    }
    index = (index + 1) & (U32)(table->capacity - 1);
  }
}
