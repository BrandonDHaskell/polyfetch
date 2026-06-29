#!/usr/bin/env bash
# Generates shared/targets.txt -- the canonical workload for the experiment.
# Env vars: COUNT (default 200), SIZE bytes (default 1048576 = 1 MiB), DELAY ms (default 0)
#
# Examples:
#   ./gen_targets.sh                              # 200 x 1 MiB, no delay
#   COUNT=500 SIZE=4194304 ./gen_targets.sh       # 500 x 4 MiB
#   COUNT=50 DELAY=100 ./gen_targets.sh           # 50 x 1 MiB, 100ms delay each
#
# IDs are zero-padded so lexicographic sort matches numeric sort (required by §8).
#
# Profiles to add later:
#   - Heterogeneous: mixed sizes, some delays, a few ?status=500 targets
#   - Giants: handful of very large payloads (shifts bottleneck from concurrency to bandwidth)
set -euo pipefail

OUT="$(dirname "$0")/targets.txt"
COUNT="${COUNT:-200}"
SIZE="${SIZE:-1048576}"
DELAY="${DELAY:-0}"

{
  echo "# polyfetch workload"
  echo "# generated $(date -u +%FT%TZ)"
  echo "# count=${COUNT} size=${SIZE} delay=${DELAY}"
  for i in $(seq -f "%05g" 1 "$COUNT"); do
    if [[ "$DELAY" -gt 0 ]]; then
      echo "${i}?size=${SIZE}&delay=${DELAY}"
    else
      echo "${i}?size=${SIZE}"
    fi
  done
} > "$OUT"

echo "wrote $OUT ($COUNT targets, ${SIZE}B each)"
