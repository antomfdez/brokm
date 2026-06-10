#!/usr/bin/env bash
# AOT golden tests: `brokm build` each cases/*.bk into a native executable, run
# it, and compare stdout+stderr to the same cases/*.expected the interpreter
# suite uses. A build that fails (e.g. a static type error) contributes its
# stderr as the output, so the static-error goldens pass unchanged.
set -u

BIN="./brokm"
DIR="$(cd "$(dirname "$0")" && pwd)/cases"
# The standard library lives in <repo>/lib; #include "std/..." resolves
# through $BROKM_HOME/lib at build time (and aot.c probes the same variable
# for the runtime sources).
BROKM_HOME="$(cd "$(dirname "$0")/.." && pwd)"
export BROKM_HOME
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

if [ ! -x "$BIN" ]; then
  echo "error: $BIN not built (run 'make' first)"
  exit 1
fi

pass=0
fail=0

for bk in "$DIR"/*.bk; do
  exp="${bk%.bk}.expected"
  [ -f "$exp" ] || continue
  name="$(basename "$bk" .bk)"
  exe="$TMP/$name"
  if out="$("$BIN" build "$bk" -o "$exe" --quiet 2>&1)"; then
    got="$("$exe" 2>&1)"
  else
    got="$out"
  fi
  want="$(cat "$exp")"
  if [ "$got" == "$want" ]; then
    echo "PASS $name (aot)"
    pass=$((pass + 1))
  else
    echo "FAIL $name (aot)"
    diff <(printf '%s\n' "$want") <(printf '%s\n' "$got") | sed 's/^/    /'
    fail=$((fail + 1))
  fi
done

echo "-----"
echo "$pass passed, $fail failed (aot)"
[ "$fail" -eq 0 ]
