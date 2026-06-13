#!/usr/bin/env bash
# Freestanding AOT tests: build a focused subset with --freestanding and compare
# either the executable output or the build-time diagnostics to their goldens.
set -u

BIN="./brokm"
DIR="$(cd "$(dirname "$0")" && pwd)/freestanding"
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
  if out="$("$BIN" build "$bk" -o "$exe" --freestanding --quiet 2>&1)"; then
    got="$("$exe" 2>&1)"
  else
    got="$out"
  fi
  want="$(cat "$exp")"
  if [ "$got" = "$want" ]; then
    echo "PASS $name (freestanding)"
    pass=$((pass + 1))
  else
    echo "FAIL $name (freestanding)"
    diff <(printf '%s\n' "$want") <(printf '%s\n' "$got") | sed 's/^/    /'
    fail=$((fail + 1))
  fi
done

cfile="$TMP/freestanding_core.c"
if "$BIN" build "$DIR/core.bk" -o "$cfile" --emit=c --freestanding --quiet &&
   grep -q '^#define BK_FREESTANDING 1$' "$cfile"; then
  echo "PASS freestanding_core (emit-c)"
  pass=$((pass + 1))
else
  echo "FAIL freestanding_core (emit-c)"
  fail=$((fail + 1))
fi

echo "-----"
echo "$pass passed, $fail failed (freestanding)"
[ "$fail" -eq 0 ]
