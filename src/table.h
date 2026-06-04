/* table.h - open-addressing hash table keyed by interned ObjString*. */
#ifndef BROKM_TABLE_H
#define BROKM_TABLE_H

#include "common.h"
#include "value.h"

typedef struct {
  ObjString *key;
  Value value;
} Entry;

typedef struct {
  int count;
  int capacity;
  Entry *entries;
} Table;

void table_init(Table *table);
void table_free(Table *table);
bool table_get(Table *table, ObjString *key, Value *out);
bool table_set(Table *table, ObjString *key, Value value);
bool table_delete(Table *table, ObjString *key);
ObjString *table_find_string(Table *table, const char *chars, int length,
                             U32 hash);

#endif /* BROKM_TABLE_H */
