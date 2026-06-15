/* output.h - the runtime's output seam.
 *
 * brokm separates *what* to print (formatting, no libc) from *where* the bytes
 * go (the sink). Every byte a program emits passes through `bk_putchar` — the
 * single function a BK_FREESTANDING host replaces with serial/VGA/syscall
 * output. Diagnostics use the separate `bk_stderr` sink (hosted-only). This is
 * the foundation the freestanding profile is built on: swap one function and
 * the whole print path follows. */
#ifndef BROKM_OUTPUT_H
#define BROKM_OUTPUT_H

#include "value.h"

/* A byte sink: where formatted output lands. */
typedef struct BkSink BkSink;
struct BkSink {
  void (*emit)(char c);
};

extern BkSink bk_stdout; /* program output; emit -> bk_putchar (swappable) */
extern BkSink bk_stderr; /* hosted diagnostic channel */

/* The one swappable stdout byte sink (bk_stdout.emit points here). A
 * BK_FREESTANDING build provides its own definition. */
void bk_putchar(char c);

void bk_sink_cstr(BkSink *s, const char *str); /* emit a NUL-terminated string */
int bk_i64_to_cstr(char *buf, size_t cap, I64 n);
int bk_u64_to_cstr(char *buf, size_t cap, U64 n, unsigned base, bool upper);
int bk_f64_to_cstr(char *buf, size_t cap, F64 x);
void bk_sink_i64(BkSink *s, I64 n);            /* emit a decimal integer */
void bk_sink_u64_base(BkSink *s, U64 n, unsigned base, bool upper);
void bk_sink_f64(BkSink *s, double x);         /* emit a %g float */
void bk_sink_value(BkSink *s, Value v);        /* emit a value's display form */

/* The object half of bk_sink_value (implemented in object.c, which owns the
 * object layouts). */
void object_print_sink(BkSink *s, Value v);

#endif /* BROKM_OUTPUT_H */
