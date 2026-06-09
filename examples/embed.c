/* embed.c - embedding brokm in a C host program (v0.11 instance API).
 *
 * Build & run:  make embed && ./embed-demo
 * It registers host natives, evaluates brokm source, calls brokm functions from
 * C, exchanges scalar and string values, reads/sets globals — and runs two
 * independent VMs side by side, proving they share nothing. */
#include <stdio.h>

#include "brokm.h"

/* A host native exposed to brokm scripts: HostAdd(a, b) -> a + b. */
static BrokmValue HostAdd(int argc, const BrokmValue *args) {
  int64_t a = argc > 0 ? brokm_as_int(args[0]) : 0;
  int64_t b = argc > 1 ? brokm_as_int(args[1]) : 0;
  return brokm_int(a + b);
}

/* A host native returning a string, built on whichever VM called it. */
static BrokmValue HostGreeting(int argc, const BrokmValue *args) {
  (void)argc;
  (void)args;
  return brokm_string(brokm_current(), "hello from C");
}

int main(void) {
  BrokmVM *vm = brokm_new();
  printf("brokm %s embedding demo\n", brokm_version());

  /* 1. Expose host functions to brokm scripts. */
  brokm_register(vm, "HostAdd", HostAdd);
  brokm_register(vm, "HostGreeting", HostGreeting);

  /* 2. Define brokm code that calls a host native, plus a global and helpers. */
  const char *src =
      "I64 Triple(I64 n) { return HostAdd(n, HostAdd(n, n)); }\n"
      "U8 Greet() { return HostGreeting(); }\n"
      "U8 Hi(U8 name) { return \"hi \" + name; }\n"
      "I64 counter = 10;\n"
      "U0 Bump() { counter = counter + 5; }\n";
  if (brokm_eval(vm, src) != BROKM_OK) {
    brokm_free(vm);
    return 1;
  }

  /* 3. Call a brokm function from C with an argument. */
  BrokmValue args[1] = {brokm_int(7)};
  BrokmValue result;
  if (brokm_call(vm, "Triple", 1, args, &result) == BROKM_OK) {
    printf("Triple(7) = %lld\n", (long long)brokm_as_int(result));
  }

  /* 4. A brokm function that returns a host-native string back to C. */
  if (brokm_call(vm, "Greet", 0, NULL, &result) == BROKM_OK) {
    printf("Greet() = %s\n", brokm_as_cstring(result));
  }

  /* 5. Pass a host string into brokm and get a concatenated string back. */
  BrokmValue sargs[1] = {brokm_string(vm, "brokm")};
  if (brokm_call(vm, "Hi", 1, sargs, &result) == BROKM_OK) {
    printf("Hi(brokm) = %s\n", brokm_as_cstring(result));
  }

  /* 6. Read and set a brokm global from C. */
  BrokmValue g;
  if (brokm_get_global(vm, "counter", &g)) {
    printf("counter = %lld\n", (long long)brokm_as_int(g));
  }
  brokm_call(vm, "Bump", 0, NULL, NULL);
  brokm_get_global(vm, "counter", &g);
  printf("after Bump = %lld\n", (long long)brokm_as_int(g));
  brokm_set_global(vm, "counter", brokm_int(100));
  brokm_get_global(vm, "counter", &g);
  printf("after set = %lld\n", (long long)brokm_as_int(g));

  /* 7. Multi-instance (v0.11): two more VMs, same global names, different
   * worlds. Each gets its own heap, globals, and interned strings; running and
   * freeing one never touches the other — or the first VM above. */
  BrokmVM *a = brokm_new();
  BrokmVM *b = brokm_new();
  brokm_eval(a, "I64 who = 1; I64 Get() { return who * 10; }");
  brokm_eval(b, "I64 who = 2; I64 Get() { return who * 10; }");
  BrokmValue ra, rb;
  brokm_call(a, "Get", 0, NULL, &ra);
  brokm_call(b, "Get", 0, NULL, &rb);
  printf("vm A Get() = %lld, vm B Get() = %lld\n",
         (long long)brokm_as_int(ra), (long long)brokm_as_int(rb));

  /* Host natives are per VM: HostAdd exists on the first VM only.
   * (Natives are ordinary globals, so brokm_get_global sees them.) */
  printf("HostAdd visible: first vm=%d, vm A=%d\n",
         brokm_get_global(vm, "HostAdd", NULL),
         brokm_get_global(a, "HostAdd", NULL));

  /* Destroy A, then keep using B and the first VM — their heaps are intact. */
  brokm_free(a);
  brokm_call(b, "Get", 0, NULL, &rb);
  brokm_get_global(vm, "counter", &g);
  printf("after freeing A: B Get() = %lld, first counter = %lld\n",
         (long long)brokm_as_int(rb), (long long)brokm_as_int(g));
  brokm_free(b);

  brokm_free(vm);
  return 0;
}
