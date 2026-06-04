/* api.c - implementation of the public brokm.h embedding API. */
#include <stdio.h>
#include <stdlib.h>

#include "brokm.h"
#include "vm.h"

#define BROKM_VERSION "0.1.0"

void brokm_init(void) { vm_init(); }
void brokm_shutdown(void) { vm_free(); }
const char *brokm_version(void) { return BROKM_VERSION; }

static BrokmResult map_result(InterpretResult result) {
  switch (result) {
    case BK_OK: return BROKM_OK;
    case BK_COMPILE_ERROR: return BROKM_COMPILE_ERROR;
    default: return BROKM_RUNTIME_ERROR;
  }
}

BrokmResult brokm_eval(const char *source) {
  return map_result(vm_interpret(source));
}

static char *read_file(const char *path) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) {
    fprintf(stderr, "brokm: could not open '%s'\n", path);
    return NULL;
  }
  fseek(file, 0L, SEEK_END);
  long size = ftell(file);
  rewind(file);

  char *buffer = (char *)malloc((size_t)size + 1);
  if (buffer == NULL) {
    fprintf(stderr, "brokm: not enough memory to read '%s'\n", path);
    fclose(file);
    return NULL;
  }
  size_t read = fread(buffer, sizeof(char), (size_t)size, file);
  buffer[read] = '\0';
  fclose(file);
  return buffer;
}

BrokmResult brokm_run_file(const char *path) {
  char *source = read_file(path);
  if (source == NULL) return BROKM_RUNTIME_ERROR;
  BrokmResult result = brokm_eval(source);
  free(source);
  return result;
}
