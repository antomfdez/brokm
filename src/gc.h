/* gc.h - mark-sweep garbage collector (v0.2).
 *
 * A precise tri-color mark-sweep collector. It walks the VM roots (value stack,
 * call frames, globals), traces references, treats the interned-string table as
 * a weak set, then sweeps the intrusive object list. Collection is driven from
 * reallocate() in memory.c and is gated by vm.gcEnabled so it never runs while
 * the compiler is building objects that are not yet rooted. */
#ifndef BROKM_GC_H
#define BROKM_GC_H

#include "object.h"
#include "value.h"

void collect_garbage(void);
void mark_object(Obj *object);
void mark_value(Value value);

#endif /* BROKM_GC_H */
