/* memory.c - allocation chokepoint implementation. */
#include <stdio.h>
#include <stdlib.h>

#include "memory.h"

void *reallocate(void *ptr, size_t oldSize, size_t newSize) {
  (void)oldSize; /* used by the GC in a later milestone */

  if (newSize == 0) {
    free(ptr);
    return NULL;
  }

  void *result = realloc(ptr, newSize);
  if (result == NULL) {
    fprintf(stderr, "brokm: out of memory\n");
    exit(70);
  }
  return result;
}
