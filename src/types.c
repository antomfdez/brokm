/* types.c - type lattice helpers (see types.h). */
#include "object.h"
#include "types.h"

Type ty(TypeKind kind) {
  Type t;
  t.kind = kind;
  t.name = NULL;
  return t;
}

Type ty_named(TypeKind kind, ObjString *name) {
  Type t;
  t.kind = kind;
  t.name = name;
  return t;
}

const char *type_name(Type t) {
  switch (t.kind) {
    case TY_UNKNOWN: return "?";
    case TY_VOID: return "U0";
    case TY_INT: return "int";
    case TY_FLOAT: return "F64";
    case TY_BOOL: return "Bool";
    case TY_STRING: return "String";
    case TY_NIL: return "nil";
    case TY_ARRAY: return "array";
    case TY_PTR: return "pointer";
    case TY_INSTANCE: return t.name != NULL ? t.name->chars : "instance";
    case TY_CLASS: return t.name != NULL ? t.name->chars : "class";
    case TY_FUNCTION: return "function";
  }
  return "?";
}

bool type_is_numeric(Type t) { return t.kind == TY_INT || t.kind == TY_FLOAT; }

/* A NULL class name acts as a wildcard so an un-named instance/class type still
 * matches a named one (keeps the checker permissive where names are unknown). */
static bool names_match(ObjString *a, ObjString *b) {
  return a == NULL || b == NULL || a == b;
}

/* Assignability under brokm's HolyC-flavored type vocabulary. The scalar type
 * keywords (U8..I64, F64, Bool) are weak storage hints — idiomatic code keeps
 * strings and pointers in `U8` slots and uses `U0` as a generic/void type — so
 * scalars, strings, pointers, and arrays interconvert freely. Strict identity
 * is enforced only for class instances and class values, which carry real type
 * identity through their name. `nil` initializes anything (it is HolyC 0). */
bool type_assignable(Type dst, Type src) {
  if (dst.kind == TY_UNKNOWN || src.kind == TY_UNKNOWN) return true;
  if (src.kind == TY_NIL) return true;
  if (dst.kind == TY_INSTANCE) {
    return src.kind == TY_INSTANCE && names_match(dst.name, src.name);
  }
  if (dst.kind == TY_CLASS) {
    return src.kind == TY_CLASS && names_match(dst.name, src.name);
  }
  /* dst is a scalar/array/ptr slot: anything but a class object fits. */
  return src.kind != TY_INSTANCE && src.kind != TY_CLASS;
}
