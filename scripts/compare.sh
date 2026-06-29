#!/usr/bin/env bash
# Runs all available clients at the same concurrency and diffs their outputs.
# Strips fetch_ms before comparison -- the only field the spec allows to differ.
# Skips clients that haven't been built yet.
#
# Usage: compare.sh [concurrency]
# Requires: jq
set -euo pipefail

CONC="${1:-8}"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

is_built() {
  case "$1" in
    go)         [[ -x clients/go/polyfetch ]] ;;
    cpp)        [[ -x clients/cpp/build/polyfetch ]] ;;
    python)     [[ -f clients/python/polyfetch.py ]] ;;
    typescript) [[ -f clients/typescript/dist/polyfetch.js ]] ;;
  esac
}

run_client() {
  local lang="$1" out="$2"
  case "$lang" in
    go)         taskset -c 4-15 clients/go/polyfetch --concurrency "$CONC" ;;
    cpp)        taskset -c 4-15 clients/cpp/build/polyfetch --concurrency "$CONC" ;;
    python)     taskset -c 4-15 python clients/python/polyfetch.py --concurrency "$CONC" ;;
    typescript) taskset -c 4-15 node clients/typescript/dist/polyfetch.js --concurrency "$CONC" ;;
  esac | jq -c 'del(.fetch_ms)' > "$out"
}

built=()
for lang in go cpp python typescript; do
  if is_built "$lang"; then
    echo "running: $lang --concurrency $CONC"
    run_client "$lang" "$TMP/${lang}.out"
    built+=("$lang")
  else
    echo "skip: $lang not built"
  fi
done

if [[ ${#built[@]} -lt 2 ]]; then
  echo "need at least two clients built to compare" >&2
  exit 1
fi

ref="${built[0]}"
fail=0
for lang in "${built[@]:1}"; do
  if diff -q "$TMP/${ref}.out" "$TMP/${lang}.out" > /dev/null; then
    echo "OK: $lang matches $ref"
  else
    echo "FAIL: $lang differs from $ref"
    diff "$TMP/${ref}.out" "$TMP/${lang}.out" | head -20
    fail=1
  fi
done
exit "$fail"
