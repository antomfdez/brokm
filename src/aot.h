/* aot.h - `brokm build`: AOT-compile a .bk file to a native executable by
 * emitting C and invoking the system C compiler. */
#ifndef BROKM_AOT_H
#define BROKM_AOT_H

#include <stdbool.h>

typedef struct {
  const char *outPath; /* -o; NULL = source basename minus .bk */
  bool emitCOnly;      /* --emit=c: write the C source only, skip cc */
  bool keepC;          /* --keep-c: keep the intermediate .c beside the exe */
  bool freestanding;   /* --freestanding: build against the reduced runtime */
  const char *cc;      /* --cc; NULL = $CC env else "cc" */
  const char *optFlag; /* optimization flag passed to cc, default "-O2" */
  const char *cflags;  /* --cflags; extra cc/linker flags appended verbatim
                          after the runtime sources (late, so -L/-l work with
                          GNU ld and --target/-O overrides win); NULL = none */
  bool verbose;        /* print the cc command line */
  bool quiet;          /* suppress progress output */
} AotOptions;

/* Compile srcPath to a native executable (or C source with --emit=c). argv0
 * locates the brokm runtime sources ($BROKM_HOME overrides the argv[0]-derived
 * location). Returns a process exit code: 0 ok, 65 compile error, 70 internal
 * error, or the C compiler's failure status. */
int aot_build(const char *srcPath, const char *argv0, const AotOptions *opt);

#endif /* BROKM_AOT_H */
