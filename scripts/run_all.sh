#!/usr/bin/env bash
set -euo pipefail

# Define benchmark matrices
LANGS=("go" "cpp" "typescript" "python")
CORES=("1" "2" "4" "8")
REPEATS="${1:-3}" # Defaults to 3 runs if not provided

# Outer loop: Languages
for LANG in "${LANGS[@]}"; do
  
  # Determine command based on language
  case "$LANG" in
    go)         CLIENT_CMD=(clients/go/polyfetch) ;;
    cpp)        CLIENT_CMD=(clients/cpp/build/polyfetch) ;;
    python)     CLIENT_CMD=(python3 clients/python/polyfetch.py) ;;
    typescript) CLIENT_CMD=(node clients/typescript/dist/polyfetch.js) ;;
    *) echo "unknown lang: $LANG" >&2; exit 2 ;;
  esac

  # Inner loop: CPU Cores / Concurrency
  for CONC in "${CORES[@]}"; do
    
    OUT_DIR="results/${LANG}/c${CONC}"
    mkdir -p "$OUT_DIR"

    echo "========================================="
    echo "Benchmarking: ${LANG} with ${CONC} core(s)"
    echo "========================================="

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
    echo ""
  done
done