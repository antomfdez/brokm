/* memory.h - the single allocation chokepoint. A future GC hooks in here. */
#ifndef BROKM_MEMORY_H
#define BROKM_MEMORY_H

#include "common.h"

#define GROW_CAPACITY(cap) ((cap) < 8 ? 8 : (cap) * 2)

#define GROW_ARRAY(type, ptr, oldCount, newCount) \
  (type *)reallocate(ptr, sizeof(type) * (oldCount), sizeof(type) * (newCount))

#define FREE_ARRAY(type, ptr, oldCount) \
  reallocate(ptr, sizeof(type) * (oldCount), 0)

#define ALLOCATE(type, count) \
  (type *)reallocate(NULL, 0, sizeof(type) * (count))

#define FREE(type, ptr) reallocate(ptr, sizeof(type), 0)

/* All dynamic memory in brokm routes through this one function so that the
 * generational GC planned for v0.3 has a single place to trigger collection. */
void *reallocate(void *ptr, size_t oldSize, size_t newSize);

#endif /* BROKM_MEMORY_H */
