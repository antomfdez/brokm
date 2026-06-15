/* output.c - the hosted default sinks and localized number formatting.
 *
 * The stdout/stderr byte sinks are hosted by default. Integer conversion is
 * libc-free so the freestanding profile can reuse it; hosted float conversion
 * still delegates to snprintf for byte-identical %g behavior. */
#include <stdio.h>
#ifdef BK_FREESTANDING
#include <float.h>
#endif

#include "output.h"

/* The stdout byte sink. Routed through the stdout FILE buffer so ordering
 * against bk_stderr (and against runtime_error's stderr) is unchanged. */
void bk_putchar(char c) { putchar((unsigned char)c); }

static void emit_stdout(char c) { bk_putchar(c); }
static void emit_stderr(char c) { fputc((unsigned char)c, stderr); }

BkSink bk_stdout = {emit_stdout};
BkSink bk_stderr = {emit_stderr};

void bk_sink_cstr(BkSink *s, const char *str) {
  for (; *str != '\0'; str++) s->emit(*str);
}

int bk_u64_to_cstr(char *buf, size_t cap, U64 n, unsigned base, bool upper) {
  const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
  char tmp[65];
  int count = 0;
  if (base < 2 || base > 16) base = 10;
  do {
    tmp[count++] = digits[n % base];
    n /= base;
  } while (n != 0);

  int len = count;
  if (cap > 0) {
    int out = 0;
    while (count > 0 && (size_t)(out + 1) < cap) buf[out++] = tmp[--count];
    buf[out] = '\0';
  }
  return len;
}

int bk_i64_to_cstr(char *buf, size_t cap, I64 n) {
  U64 magnitude;
  bool neg = n < 0;
  if (neg) {
    magnitude = (U64)(-(n + 1)) + 1u;
  } else {
    magnitude = (U64)n;
  }

  char tmp[65];
  int digits = bk_u64_to_cstr(tmp, sizeof(tmp), magnitude, 10, false);
  int len = digits + (neg ? 1 : 0);
  if (cap > 0) {
    size_t out = 0;
    if (neg && out + 1 < cap) buf[out++] = '-';
    for (int i = 0; i < digits && out + 1 < cap; i++) buf[out++] = tmp[i];
    buf[out] = '\0';
  }
  return len;
}

#ifdef BK_FREESTANDING
static int copy_cstr(char *buf, size_t cap, const char *s) {
  int len = 0;
  while (s[len] != '\0') {
    if (cap > 0 && (size_t)(len + 1) < cap) buf[len] = s[len];
    len++;
  }
  if (cap > 0) buf[(size_t)len < cap ? (size_t)len : cap - 1] = '\0';
  return len;
}

int bk_f64_to_cstr(char *buf, size_t cap, F64 x) {
  if (x != x) {
    return copy_cstr(buf, cap, "nan");
  }
  if (x > DBL_MAX) {
    return copy_cstr(buf, cap, "inf");
  }
  if (x < -DBL_MAX) {
    return copy_cstr(buf, cap, "-inf");
  }

  size_t out = 0;
  if (x < 0.0) {
    if (cap > 0 && out + 1 < cap) buf[out] = '-';
    out++;
    x = -x;
  }

  I64 whole = (I64)x;
  char wholeBuf[32];
  int wholeLen = bk_i64_to_cstr(wholeBuf, sizeof(wholeBuf), whole);
  for (int i = 0; i < wholeLen; i++) {
    if (cap > 0 && out + 1 < cap) buf[out] = wholeBuf[i];
    out++;
  }

  F64 frac = x - (F64)whole;
  if (frac > 0.0) {
    char fracBuf[6];
    int fracLen = 0;
    for (int i = 0; i < 6 && frac > 0.0; i++) {
      frac *= 10.0;
      int digit = (int)frac;
      fracBuf[fracLen++] = (char)('0' + digit);
      frac -= (F64)digit;
    }
    while (fracLen > 0 && fracBuf[fracLen - 1] == '0') fracLen--;
    if (fracLen > 0) {
      if (cap > 0 && out + 1 < cap) buf[out] = '.';
      out++;
    }
    for (int i = 0; i < fracLen; i++) {
      if (cap > 0 && out + 1 < cap) buf[out] = fracBuf[i];
      out++;
    }
  }
  if (cap > 0) buf[out < cap ? out : cap - 1] = '\0';
  return (int)out;
}
#else
int bk_f64_to_cstr(char *buf, size_t cap, F64 x) {
  int len = snprintf(buf, cap, "%g", x);
  return len < 0 ? 0 : len;
}
#endif

void bk_sink_i64(BkSink *s, I64 n) {
  char buf[24]; /* -9223372036854775808 + NUL fits in 21 */
  bk_i64_to_cstr(buf, sizeof(buf), n);
  bk_sink_cstr(s, buf);
}

void bk_sink_u64_base(BkSink *s, U64 n, unsigned base, bool upper) {
  char buf[65];
  bk_u64_to_cstr(buf, sizeof(buf), n, base, upper);
  bk_sink_cstr(s, buf);
}

void bk_sink_f64(BkSink *s, double x) {
  char buf[32];
  bk_f64_to_cstr(buf, sizeof(buf), x);
  bk_sink_cstr(s, buf);
}

void bk_sink_value(BkSink *s, Value v) {
  switch (v.type) {
    case VAL_NIL:   bk_sink_cstr(s, "nil"); break;
    case VAL_BOOL:  bk_sink_cstr(s, AS_BOOL(v) ? "TRUE" : "FALSE"); break;
    case VAL_INT:   bk_sink_i64(s, AS_INT(v)); break;
    case VAL_FLOAT: bk_sink_f64(s, AS_FLOAT(v)); break;
    case VAL_OBJ:   object_print_sink(s, v); break;
    case VAL_PTR:   bk_sink_cstr(s, AS_PTR(v) != NULL ? "<ptr>" : "NULL"); break;
    default:        bk_sink_cstr(s, "<unknown>"); break;
  }
}
