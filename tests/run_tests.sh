#!/usr/bin/env bash
# Golden tests: run each cases/*.bk and compare stdout+stderr to cases/*.expected.
set -u

BIN="./brokm"
DIR="$(cd "$(dirname "$0")" && pwd)/cases"
# The standard library lives in <repo>/lib; #include "std/..." resolves
# through $BROKM_HOME/lib (lexer.c handle_include).
BROKM_HOME="$(cd "$(dirname "$0")/.." && pwd)"
export BROKM_HOME

if [ ! -x "$BIN" ]; then
  echo "error: $BIN not built (run 'make' first)"
  exit 1
fi

pass=0
fail=0

for bk in "$DIR"/*.bk; do
  exp="${bk%.bk}.expected"
  [ -f "$exp" ] || continue
  got="$("$BIN" "$bk" 2>&1)"
  want="$(cat "$exp")"
  if [ "$got" == "$want" ]; then
    echo "PASS $(basename "$bk")"
    pass=$((pass + 1))
  else
    echo "FAIL $(basename "$bk")"
    diff <(printf '%s\n' "$want") <(printf '%s\n' "$got") | sed 's/^/    /'
    fail=$((fail + 1))
  fi
done

echo "-----"
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
