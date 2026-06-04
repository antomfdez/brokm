/* memory.c - allocation chokepoint that drives garbage collection. */
#include <stdio.h>
#include <stdlib.h>

#include "gc.h"
#include "memory.h"
#include "vm.h"

void *reallocate(void *ptr, size_t oldSize, size_t newSize) {
  vm.bytesAllocated += newSize - oldSize;

  /* Only growth can exhaust the heap, so only growth triggers collection.
   * Frees (newSize < oldSize) never re-enter the collector, which keeps the
   * sweep phase - which frees objects through here - from recursing. */
  if (newSize > oldSize) {
#ifdef BK_DEBUG_STRESS_GC
    collect_garbage();
#endif
    if (vm.bytesAllocated > vm.nextGC) collect_garbage();
  }

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
