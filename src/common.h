/* common.h - shared types and tunables for the brokm runtime. */
#ifndef BROKM_COMMON_H
#define BROKM_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* HolyC-style fixed-width aliases used internally by the runtime. */
typedef uint8_t  U8;
typedef uint16_t U16;
typedef uint32_t U32;
typedef uint64_t U64;
typedef int8_t   I8;
typedef int16_t  I16;
typedef int32_t  I32;
typedef int64_t  I64;
typedef double   F64;

/* VM tunables. Stack is sized so each call frame gets a full slot window. */
#define BK_FRAMES_MAX 64
#define BK_SLOT_COUNT 256
#define BK_STACK_MAX  (BK_FRAMES_MAX * BK_SLOT_COUNT)

/* Optional verbose diagnostics (off by default). */
/* #define BK_DEBUG_PRINT_CODE */
/* #define BK_DEBUG_TRACE_EXECUTION */

#endif /* BROKM_COMMON_H */
