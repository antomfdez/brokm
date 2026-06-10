/* aot.c - `brokm build` driver: front-end + C emission + cc invocation.
 *
 * Pipeline: read .bk -> parse -> typecheck -> compile_program (the same
 * front-end `brokm run` uses) -> cemit_program writes a C translation unit ->
 * the system C compiler links it with the brokm runtime sources (-DBK_NO_JIT)
 * into a standalone native executable. */
#ifdef __linux__
#define _XOPEN_SOURCE 700 /* realpath with -std=c99 on glibc */
#endif

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aot.h"
#include "ast.h"
#include "cemit.h"
#include "compiler.h"
#include "parser.h"
#include "typecheck.h"
#include "vm.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* Runtime translation units linked into every AOT binary: all of src/ except
 * main.c (the CLI) and aot.c/cemit.c (the build tool itself). The front end
 * stays in because vm.c's interpret() references it, and the interpreter stays
 * in because dynamically assembled functions (the Assemble native) run on it. */
static const char *const RUNTIME_SRCS[] = {
    "api.c",    "ast.c",   "chunk.c",     "compiler.c", "debug.c",
    "gc.c",     "jit.c",   "jit_arm64.c", "jit_x64.c",  "lexer.c",
    "memory.c", "natives.c", "object.c",  "parser.c",   "table.c",
    "typecheck.c", "types.c", "value.c",  "vm.c",
};
#define RUNTIME_SRC_COUNT (sizeof(RUNTIME_SRCS) / sizeof(RUNTIME_SRCS[0]))

static char *read_file(const char *path) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) {
    fprintf(stderr, "brokm build: could not open '%s'\n", path);
    return NULL;
  }
  fseek(file, 0L, SEEK_END);
  long size = ftell(file);
  rewind(file);
  char *buffer = (char *)malloc((size_t)size + 1);
  if (buffer == NULL) {
    fprintf(stderr, "brokm build: not enough memory to read '%s'\n", path);
    fclose(file);
    return NULL;
  }
  size_t read = fread(buffer, sizeof(char), (size_t)size, file);
  buffer[read] = '\0';
  fclose(file);
  return buffer;
}

/* Locate the brokm source tree (include/ + src/) that AOT binaries compile
 * against: $BROKM_HOME if set, else the directory containing the brokm binary
 * (the repo root, where `make` puts it). */
static bool resolve_home(const char *argv0, char *home, size_t cap) {
  const char *env = getenv("BROKM_HOME");
  if (env != NULL && env[0] != '\0') {
    snprintf(home, cap, "%s", env);
  } else {
    char resolved[PATH_MAX];
    if (realpath(argv0, resolved) == NULL) {
      fprintf(stderr,
              "brokm build: cannot locate the brokm runtime from '%s'; "
              "set BROKM_HOME to the brokm source tree\n",
              argv0);
      return false;
    }
    char *slash = strrchr(resolved, '/');
    if (slash != NULL) *slash = '\0';
    snprintf(home, cap, "%s", resolved);
  }

  char probe[PATH_MAX + 16]; /* room for home + "/src/vm.c" (gcc -Wformat-truncation) */
  snprintf(probe, sizeof(probe), "%s/src/vm.c", home);
  FILE *f = fopen(probe, "rb");
  if (f == NULL) {
    fprintf(stderr,
            "brokm build: runtime sources not found under '%s' "
            "(missing src/vm.c); set BROKM_HOME to the brokm source tree\n",
            home);
    return false;
  }
  fclose(f);
  return true;
}

/* Append printf-style text to a fixed buffer; flags overflow instead of
 * truncating silently. */
static bool buf_appendf(char *buf, size_t cap, size_t *len, const char *fmt,
                        ...) {
  va_list args;
  va_start(args, fmt);
  int n = vsnprintf(buf + *len, cap - *len, fmt, args);
  va_end(args);
  if (n < 0 || (size_t)n >= cap - *len) return false;
  *len += (size_t)n;
  return true;
}

/* Single-quote a path for /bin/sh: ' -> '\'' */
static bool buf_append_quoted(char *buf, size_t cap, size_t *len,
                              const char *path) {
  if (!buf_appendf(buf, cap, len, "'")) return false;
  for (const char *p = path; *p != '\0'; p++) {
    if (*p == '\'') {
      if (!buf_appendf(buf, cap, len, "'\\''")) return false;
    } else {
      if (!buf_appendf(buf, cap, len, "%c", *p)) return false;
    }
  }
  return buf_appendf(buf, cap, len, "'");
}

/* Default output name: source basename minus a trailing ".bk". */
static void default_out(const char *srcPath, char *out, size_t cap) {
  const char *slash = strrchr(srcPath, '/');
  const char *base = slash != NULL ? slash + 1 : srcPath;
  snprintf(out, cap, "%s", base);
  size_t n = strlen(out);
  if (n > 3 && strcmp(out + n - 3, ".bk") == 0) out[n - 3] = '\0';
}

int aot_build(const char *srcPath, const char *argv0, const AotOptions *opt) {
  char home[PATH_MAX];
  if (!opt->emitCOnly && !resolve_home(argv0, home, sizeof(home))) return 70;

  char *source = read_file(srcPath);
  if (source == NULL) return 70;

  /* #includes resolve relative to the source file, like brokm_run_file. */
  char dir[PATH_MAX];
  const char *slash = strrchr(srcPath, '/');
  if (slash == NULL) {
    snprintf(dir, sizeof(dir), ".");
  } else {
    int len = (int)(slash - srcPath);
    if (len == 0) len = 1;
    snprintf(dir, sizeof(dir), "%.*s", len, srcPath);
  }

  VM *v = vm_new(); /* gcEnabled stays false: nothing runs at build time */
  if (v == NULL) {
    fprintf(stderr, "brokm build: out of memory\n");
    free(source);
    return 70;
  }

  int code = 65;
  StmtList program;
  if (!parse(source, dir, &program)) {
    free_stmtlist(&program);
    goto done;
  }
  if (!typecheck(&program)) {
    free_stmtlist(&program);
    goto done;
  }
  ObjFunction *script = compile_program(&program);
  free_stmtlist(&program);
  if (script == NULL) goto done;

  char outPath[PATH_MAX];
  if (opt->outPath != NULL) {
    snprintf(outPath, sizeof(outPath), "%s", opt->outPath);
  } else {
    default_out(srcPath, outPath, sizeof(outPath));
  }

  /* --emit=c writes <out>.c (or the -o path verbatim if it names a .c file);
   * executable builds use <out>.aot.c so a hand-written <out>.c next to the
   * program can never be clobbered or deleted. */
  char cPath[PATH_MAX + 8];
  size_t outLen = strlen(outPath);
  if (opt->emitCOnly && outLen > 2 && strcmp(outPath + outLen - 2, ".c") == 0) {
    snprintf(cPath, sizeof(cPath), "%s", outPath);
  } else if (opt->emitCOnly) {
    snprintf(cPath, sizeof(cPath), "%s.c", outPath);
  } else {
    snprintf(cPath, sizeof(cPath), "%s.aot.c", outPath);
  }

  FILE *cFile = fopen(cPath, "w");
  if (cFile == NULL) {
    fprintf(stderr, "brokm build: could not write '%s'\n", cPath);
    code = 70;
    goto done;
  }
  bool emitted = cemit_program(script, cFile);
  fclose(cFile);
  if (!emitted) {
    remove(cPath);
    code = 70;
    goto done;
  }

  if (opt->emitCOnly) {
    if (!opt->quiet) printf("brokm build: wrote %s\n", cPath);
    code = 0;
    goto done;
  }

  /* Assemble the cc command. */
  const char *cc = opt->cc;
  if (cc == NULL) cc = getenv("CC");
  if (cc == NULL || cc[0] == '\0') cc = "cc";

  static char cmd[32768];
  size_t len = 0;
  bool fit = buf_appendf(cmd, sizeof(cmd), &len, "%s -std=c99 %s -DBK_NO_JIT ",
                         cc, opt->optFlag);
  fit = fit && buf_appendf(cmd, sizeof(cmd), &len, "-I");
  fit = fit && buf_append_quoted(cmd, sizeof(cmd), &len, home);
  fit = fit && buf_appendf(cmd, sizeof(cmd), &len, "/include -I");
  fit = fit && buf_append_quoted(cmd, sizeof(cmd), &len, home);
  fit = fit && buf_appendf(cmd, sizeof(cmd), &len, "/src ");
  fit = fit && buf_append_quoted(cmd, sizeof(cmd), &len, cPath);
  for (size_t i = 0; fit && i < RUNTIME_SRC_COUNT; i++) {
    fit = fit && buf_appendf(cmd, sizeof(cmd), &len, " ");
    fit = fit && buf_append_quoted(cmd, sizeof(cmd), &len, home);
    fit = fit && buf_appendf(cmd, sizeof(cmd), &len, "/src/%s", RUNTIME_SRCS[i]);
  }
  fit = fit && buf_appendf(cmd, sizeof(cmd), &len, " -lm -o ");
  fit = fit && buf_append_quoted(cmd, sizeof(cmd), &len, outPath);
  if (!fit) {
    fprintf(stderr, "brokm build: command line too long\n");
    remove(cPath);
    code = 70;
    goto done;
  }

  if (opt->verbose) printf("brokm build: %s\n", cmd);
  int rc = system(cmd);
  if (rc != 0) {
    fprintf(stderr, "brokm build: C compiler failed (see errors above)\n");
    if (!opt->keepC) remove(cPath);
    code = 1;
    goto done;
  }
  if (!opt->keepC) remove(cPath);
  if (!opt->quiet) printf("brokm build: wrote %s\n", outPath);
  code = 0;

done:
  vm_destroy(v);
  free(source);
  return code;
}
