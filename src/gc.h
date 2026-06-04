/* gc.h - generational mark-sweep garbage collector (v0.3).
 *
 * Non-moving, two generations. Objects start young (gen 0); survivors of a
 * minor collection are promoted to old (gen 1). A *minor* collection reclaims
 * only young garbage and skips freeing old objects; a *major* collection is a
 * full heap mark-sweep. Both are precise: roots are the value stack, call
 * frames, and the globals table, and the interned-string table is a weak set.
 *
 * Why non-moving: a copying young space would require updating every pointer in
 * the system (stack, frames, constants, C-locals during natives). The current
 * object model has no benefit from moving, so we keep the intrusive-list design.
 *
 * Write barrier: gc_write_barrier() records an old object that has been made to
 * point at a young one, so minor collections can treat it as a root. brokm has
 * no mutable aggregate objects yet, so it currently has no call sites and the
 * remembered set stays empty; it is the documented extension point for arrays
 * and structs. Minor GC is correct today because every reference to a young
 * object originates from a root, which minor GC always scans. */
#ifndef BROKM_GC_H
#define BROKM_GC_H

#include "object.h"
#include "value.h"

/* Run a collection. `major` true = full heap; false = young generation only. */
void collect_garbage(bool major);

void mark_object(Obj *object);
void mark_value(Value value);

/* Record old->young edge `owner` -> `value` for the next minor collection.
 * Call after storing `value` into a heap object `owner`. (No call sites yet.) */
void gc_write_barrier(Obj *owner, Value value);

#endif /* BROKM_GC_H */
