/* embed.c - embedding brokm in a C host program (v0.9 API).
 *
 * Build & run:  make embed && ./embed-demo
 * It registers host natives, evaluates brokm source, calls brokm functions from
 * C, exchanges scalar and string values, and reads/sets globals. */
#include <stdio.h>

#include "brokm.h"

/* A host native exposed to brokm scripts: HostAdd(a, b) -> a + b. */
static BrokmValue HostAdd(int argc, const BrokmValue *args) {
  int64_t a = argc > 0 ? brokm_as_int(args[0]) : 0;
  int64_t b = argc > 1 ? brokm_as_int(args[1]) : 0;
  return brokm_int(a + b);
}

/* A host native returning a string. */
static BrokmValue HostGreeting(int argc, const BrokmValue *args) {
  (void)argc;
  (void)args;
  return brokm_string("hello from C");
}

int main(void) {
  brokm_init();
  printf("brokm %s embedding demo\n", brokm_version());

  /* 1. Expose host functions to brokm scripts. */
  brokm_register("HostAdd", HostAdd);
  brokm_register("HostGreeting", HostGreeting);

  /* 2. Define brokm code that calls a host native, plus a global and helpers. */
  const char *src =
      "I64 Triple(I64 n) { return HostAdd(n, HostAdd(n, n)); }\n"
      "U8 Greet() { return HostGreeting(); }\n"
      "U8 Hi(U8 name) { return \"hi \" + name; }\n"
      "I64 counter = 10;\n"
      "U0 Bump() { counter = counter + 5; }\n";
  if (brokm_eval(src) != BROKM_OK) {
    brokm_shutdown();
    return 1;
  }

  /* 3. Call a brokm function from C with an argument. */
  BrokmValue args[1] = {brokm_int(7)};
  BrokmValue result;
  if (brokm_call("Triple", 1, args, &result) == BROKM_OK) {
    printf("Triple(7) = %lld\n", (long long)brokm_as_int(result));
  }

  /* 4. A brokm function that returns a host-native string back to C. */
  if (brokm_call("Greet", 0, NULL, &result) == BROKM_OK) {
    printf("Greet() = %s\n", brokm_as_cstring(result));
  }

  /* 5. Pass a host string into brokm and get a concatenated string back. */
  BrokmValue sargs[1] = {brokm_string("brokm")};
  if (brokm_call("Hi", 1, sargs, &result) == BROKM_OK) {
    printf("Hi(brokm) = %s\n", brokm_as_cstring(result));
  }

  /* 6. Read and set a brokm global from C. */
  BrokmValue g;
  if (brokm_get_global("counter", &g)) {
    printf("counter = %lld\n", (long long)brokm_as_int(g));
  }
  brokm_call("Bump", 0, NULL, NULL);
  brokm_get_global("counter", &g);
  printf("after Bump = %lld\n", (long long)brokm_as_int(g));
  brokm_set_global("counter", brokm_int(100));
  brokm_get_global("counter", &g);
  printf("after set = %lld\n", (long long)brokm_as_int(g));

  brokm_shutdown();
  return 0;
}
