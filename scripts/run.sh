#!/usr/bin/env bash
# Runs one client at the given concurrency level.
# Discards the first run (warmup) then captures timed repeats under results/.
#
# Usage: run.sh <lang> <concurrency> [repeats]
#   lang:        go | cpp | python | typescript
#   concurrency: passed as --concurrency to the client
#   repeats:     number of timed runs to capture (default 5)
#
# Output: results/<lang>/c<concurrency>/run-N.{out,time,err}
#   .out  -- client stdout (the JSON lines report)
#   .time -- wall-clock seconds (from /usr/bin/time -f '%e')
#   .err  -- client stderr
set -euo pipefail

LANG="${1:?lang required: go|cpp|python|typescript}"
CONC="${2:?concurrency required}"
REPEATS="${3:-5}"

case "$LANG" in
  go)         CLIENT_CMD=(clients/go/polyfetch) ;;
  cpp)        CLIENT_CMD=(clients/cpp/build/polyfetch) ;;
  python)     CLIENT_CMD=(python clients/python/polyfetch.py) ;;
  typescript) CLIENT_CMD=(node clients/typescript/dist/polyfetch.js) ;;
  *) echo "unknown lang: $LANG" >&2; exit 2 ;;
esac

OUT_DIR="results/${LANG}/c${CONC}"
mkdir -p "$OUT_DIR"

echo "warmup: ${LANG} --concurrency ${CONC} (discarded)"
taskset -c 4-15 "${CLIENT_CMD[@]}" --concurrency "$CONC" > /dev/null

echo "measuring: ${REPEATS} runs -> ${OUT_DIR}/"
for i in $(seq 1 "$REPEATS"); do
  /usr/bin/time -f '%e' -o "${OUT_DIR}/run-${i}.time" \
    taskset -c 4-15 "${CLIENT_CMD[@]}" --concurrency "$CONC" \
    > "${OUT_DIR}/run-${i}.out" \
    2> "${OUT_DIR}/run-${i}.err"
  echo "  run ${i}: $(cat "${OUT_DIR}/run-${i}.time")s"
done

echo "done: ${OUT_DIR}/run-{1..${REPEATS}}.{out,time,err}"
