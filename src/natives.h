/* natives.h - built-in functions and the shared print formatter. */
#ifndef BROKM_NATIVES_H
#define BROKM_NATIVES_H

#include "object.h"
#include "value.h"

/* Standard-native registry, shared by natives.c and the type checker. The
 * freestanding profile keeps only the core runtime/self-hosting subset. */
#define BK_NATIVE_CORE_LIST(X)                                              \
  X("Print", native_print)                                                  \
  X("PrintErr", native_print_err)                                           \
  X("GcCollect", native_gc_collect)                                         \
  X("GcMinor", native_gc_minor)                                             \
  X("GcDisable", native_gc_disable)                                         \
  X("GcEnable", native_gc_enable)                                           \
  X("Len", native_len)                                                      \
  X("Append", native_append)                                                \
  X("MapNew", native_map_new)                                               \
  X("MapSet", native_map_set)                                               \
  X("MapGet", native_map_get)                                               \
  X("MapHas", native_map_has)                                               \
  X("MapDelete", native_map_delete)                                         \
  X("MapLen", native_map_len)                                               \
  X("MapKeys", native_map_keys)                                             \
  X("MAlloc", native_malloc)                                                \
  X("Free", native_free)                                                    \
  X("PeekU8", native_peek_u8)                                               \
  X("PokeU8", native_poke_u8)                                               \
  X("PeekI64", native_peek_i64)                                             \
  X("PokeI64", native_poke_i64)                                             \
  X("PeekF64", native_peek_f64)                                             \
  X("PokeF64", native_poke_f64)                                             \
  X("PeekPtr", native_peek_ptr)                                             \
  X("PokePtr", native_poke_ptr)                                             \
  X("CharAt", native_char_at)                                               \
  X("Chr", native_chr)                                                      \
  X("Substr", native_substr)                                                \
  X("IndexOf", native_index_of)                                             \
  X("ToInt", native_to_int)                                                 \
  X("ToStr", native_to_str)                                                 \
  X("Abs", native_abs)                                                      \
  X("Min", native_min)                                                      \
  X("Max", native_max)                                                      \
  X("Opcode", native_opcode)                                                \
  X("Assemble", native_assemble)                                            \
  X("MakeClass", native_make_class)                                         \
  X("AddMethod", native_add_method)                                         \
  X("ChunkCode", native_chunk_code)                                         \
  X("ChunkConsts", native_chunk_consts)                                     \
  X("ValueDesc", native_value_desc)

#define BK_NATIVE_HOSTED_ONLY_LIST(X)                                       \
  X("ReadFile", native_read_file)                                           \
  X("WriteFile", native_write_file)                                         \
  X("Sqrt", native_sqrt)                                                    \
  X("Pow", native_pow)                                                      \
  X("Floor", native_floor)                                                  \
  X("Ceil", native_ceil)                                                    \
  X("Args", native_args)                                                    \
  X("Env", native_env)                                                      \
  X("Exit", native_exit)                                                    \
  X("Shell", native_shell)                                                  \
  X("ShellStr", native_shell_str)                                           \
  X("Input", native_input)                                                  \
  X("Time", native_time)                                                    \
  X("TimeMs", native_time_ms)                                               \
  X("Sleep", native_sleep)                                                  \
  X("AppendFile", native_append_file)                                       \
  X("FileExists", native_file_exists)

/* printf-style formatter shared by the OP_PRINT statement and Print(). The
 * first argument is the format string; remaining args fill its specifiers. */
Value bk_format(int argc, Value *args);

/* Register the standard natives into the VM's global table. */
void natives_register(void);

/* Stash the script's command-line arguments for Args(). Called once by the
 * driver (main.c, or the AOT-emitted main) before any code runs; the pointers
 * must stay valid for the life of the process (argv does). */
void natives_set_args(int argc, char **argv);

#endif /* BROKM_NATIVES_H */
