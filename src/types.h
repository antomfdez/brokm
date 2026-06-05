/* types.h - the static type lattice used by the v0.4 type checker.
 *
 * Types are small PODs passed by value; they describe the *static* type of a
 * declaration or expression, separate from the runtime Value tag. brokm uses
 * gradual typing: TY_UNKNOWN is a top type compatible with everything, so code
 * the checker cannot reason about precisely is never wrongly rejected. */
#ifndef BROKM_TYPES_H
#define BROKM_TYPES_H

#include "common.h"
#include "value.h" /* for the ObjString forward declaration */

typedef enum {
  TY_UNKNOWN,  /* gradual top: assignable to/from anything */
  TY_VOID,     /* U0 */
  TY_INT,      /* U8..U64, I8..I64 collapse - brokm ints are I64 at runtime */
  TY_FLOAT,    /* F64 */
  TY_BOOL,
  TY_STRING,
  TY_NIL,
  TY_ARRAY,
  TY_PTR,
  TY_INSTANCE, /* class instance; name = class name */
  TY_CLASS,    /* the class constructor value; name = class name */
  TY_FUNCTION  /* a function value */
} TypeKind;

typedef struct {
  TypeKind kind;
  ObjString *name; /* class name for TY_INSTANCE / TY_CLASS, else NULL */
} Type;

Type ty(TypeKind kind);
Type ty_named(TypeKind kind, ObjString *name);

const char *type_name(Type t);
bool type_is_numeric(Type t);

/* Gradual assignability: can a value of type `src` initialize/assign a slot of
 * type `dst`? UNKNOWN on either side is always compatible. */
bool type_assignable(Type dst, Type src);

#endif /* BROKM_TYPES_H */
