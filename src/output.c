/* output.c - the hosted default sinks and the libc-localized number formatting.
 *
 * Everything libc-dependent in the print path lives here: the stdout/stderr
 * byte sinks and the snprintf-based integer/float conversions. A future
 * BK_FREESTANDING build excludes this TU and supplies its own `bk_putchar`
 * plus libc-free number formatting; nothing else in the runtime references
 * stdio for program output. */
#include <stdio.h>

#include "output.h"

/* The swappable stdout byte sink: the one function a freestanding host
 * replaces. Routed through the stdout FILE buffer so ordering against
 * bk_stderr (and against runtime_error's stderr) is unchanged. */
void bk_putchar(char c) { putchar((unsigned char)c); }

static void emit_stdout(char c) { bk_putchar(c); }
static void emit_stderr(char c) { fputc((unsigned char)c, stderr); }

BkSink bk_stdout = {emit_stdout};
BkSink bk_stderr = {emit_stderr};

void bk_sink_cstr(BkSink *s, const char *str) {
  for (; *str != '\0'; str++) s->emit(*str);
}

void bk_sink_i64(BkSink *s, long long n) {
  char buf[24]; /* -9223372036854775808 + NUL fits in 21 */
  int len = snprintf(buf, sizeof(buf), "%lld", n);
  for (int i = 0; i < len; i++) s->emit(buf[i]);
}

void bk_sink_f64(BkSink *s, double x) {
  char buf[32];
  int len = snprintf(buf, sizeof(buf), "%g", x);
  for (int i = 0; i < len; i++) s->emit(buf[i]);
}

void bk_sink_value(BkSink *s, Value v) {
  switch (v.type) {
    case VAL_NIL:   bk_sink_cstr(s, "nil"); break;
    case VAL_BOOL:  bk_sink_cstr(s, AS_BOOL(v) ? "TRUE" : "FALSE"); break;
    case VAL_INT:   bk_sink_i64(s, (long long)AS_INT(v)); break;
    case VAL_FLOAT: bk_sink_f64(s, AS_FLOAT(v)); break;
    case VAL_OBJ:   object_print_sink(s, v); break;
    case VAL_PTR:   bk_sink_cstr(s, AS_PTR(v) != NULL ? "<ptr>" : "NULL"); break;
    default:        bk_sink_cstr(s, "<unknown>"); break;
  }
}
