/* main.c - the brokm command-line driver: run a file or start a REPL. */
#include <stdio.h>
#include <string.h>

#include "brokm.h"

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

int main(int argc, char **argv) {
  BrokmVM *vm = brokm_new();
  if (vm == NULL) {
    fprintf(stderr, "brokm: out of memory\n");
    return 70;
  }

  BrokmResult result = BROKM_OK;
  if (argc == 1) {
    repl(vm);
  } else if (argc == 2) {
    if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0) {
      printf("brokm %s\n", brokm_version());
    } else {
      result = brokm_run_file(vm, argv[1]);
    }
  } else {
    fprintf(stderr, "Usage: brokm [path.bk | --version]\n");
    brokm_free(vm);
    return 64;
  }

  brokm_free(vm);
  return (int)result;
}
