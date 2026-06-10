/* main.c - the brokm command-line driver.
 *
 *   brokm                       start a REPL
 *   brokm file.bk               run a file (legacy spelling, kept working)
 *   brokm run file.bk           run a file
 *   brokm build file.bk [opts]  AOT-compile to a native executable
 *   brokm --version | -v        print the version
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aot.h"
#include "brokm.h"

static void usage(FILE *out) {
  fputs(
      "Usage: brokm [command] ...\n"
      "  brokm                      start a REPL\n"
      "  brokm <path.bk>            run a file\n"
      "  brokm run <path.bk>        run a file\n"
      "  brokm build <path.bk> [-o <out>] [--emit=c] [--keep-c]\n"
      "                        [--cc <compiler>] [-O0|-O1|-O2|-O3]\n"
      "                        [--verbose] [--quiet]\n"
      "  brokm --version | -v       print the version\n",
      out);
}

static void repl(BrokmVM *vm) {
  char line[2048];
  printf("brokm %s - REPL (Ctrl-D to exit)\n", brokm_version());
  for (;;) {
    printf("brokm> ");
    if (!fgets(line, sizeof(line), stdin)) {
      printf("\n");
      break;
    }
    brokm_eval(vm, line);
  }
}

static int run_file(const char *path) {
  BrokmVM *vm = brokm_new();
  if (vm == NULL) {
    fprintf(stderr, "brokm: out of memory\n");
    return 70;
  }
  BrokmResult result = brokm_run_file(vm, path);
  brokm_free(vm);
  return (int)result;
}

static int cmd_build(int argc, char **argv) {
  AotOptions opt = {0};
  opt.optFlag = "-O2";
  const char *src = NULL;

  for (int i = 2; i < argc; i++) {
    const char *arg = argv[i];
    if (strcmp(arg, "-o") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "brokm build: -o needs an argument\n");
        return 64;
      }
      opt.outPath = argv[++i];
    } else if (strcmp(arg, "--cc") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "brokm build: --cc needs an argument\n");
        return 64;
      }
      opt.cc = argv[++i];
    } else if (strcmp(arg, "--emit=c") == 0) {
      opt.emitCOnly = true;
    } else if (strcmp(arg, "--keep-c") == 0) {
      opt.keepC = true;
    } else if (strcmp(arg, "-O0") == 0 || strcmp(arg, "-O1") == 0 ||
               strcmp(arg, "-O2") == 0 || strcmp(arg, "-O3") == 0) {
      opt.optFlag = arg;
    } else if (strcmp(arg, "--verbose") == 0) {
      opt.verbose = true;
    } else if (strcmp(arg, "--quiet") == 0) {
      opt.quiet = true;
    } else if (arg[0] == '-') {
      fprintf(stderr, "brokm build: unknown option '%s'\n", arg);
      usage(stderr);
      return 64;
    } else if (src == NULL) {
      src = arg;
    } else {
      fprintf(stderr, "brokm build: more than one input file\n");
      return 64;
    }
  }

  if (src == NULL) {
    fprintf(stderr, "brokm build: no input file\n");
    usage(stderr);
    return 64;
  }
  return aot_build(src, argv[0], &opt);
}

int main(int argc, char **argv) {
  if (argc >= 2 && strcmp(argv[1], "build") == 0) return cmd_build(argc, argv);

  if (argc >= 2 && strcmp(argv[1], "run") == 0) {
    if (argc != 3) {
      fprintf(stderr, "Usage: brokm run <path.bk>\n");
      return 64;
    }
    return run_file(argv[2]);
  }

  if (argc == 1) {
    BrokmVM *vm = brokm_new();
    if (vm == NULL) {
      fprintf(stderr, "brokm: out of memory\n");
      return 70;
    }
    repl(vm);
    brokm_free(vm);
    return 0;
  }

  if (argc == 2) {
    if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0) {
      printf("brokm %s\n", brokm_version());
      return 0;
    }
    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
      usage(stdout);
      return 0;
    }
    return run_file(argv[1]); /* legacy: brokm file.bk */
  }

  usage(stderr);
  return 64;
}
