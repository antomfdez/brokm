/* output.h - the runtime's output seam.
 *
 * brokm separates *what* to print (formatting, no libc) from *where* the bytes
 * go (the sink). Every byte a program emits passes through `bk_putchar`,
 * which is the seam a later -nostdlib host can replace with serial/VGA/syscall
 * output. Diagnostics use the separate `bk_stderr` sink (hosted-only). */
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

/* The one stdout byte sink used by program output (bk_stdout.emit points here).
 * Today's hosted/freestanding AOT builds use output.c's default; a future
 * -nostdlib host can replace this seam with serial/VGA/syscall output. */
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
