#!/usr/bin/env bash
#
# gen_targets.sh — generate a reproducible workload file for polyfetch.
#
# Produces shared/targets.txt (one target per line) for a chosen PROFILE. The
# workload is fully determined by (PROFILE, SEED, COUNT, params): the same
# inputs always produce a BYTE-IDENTICAL file, on any machine, any OS, any bash
# version. That reproducibility is the whole point — it lets the same workload
# be re-run after code changes and replayed on different hardware.
#
# Reproducibility design:
#   * Randomness comes from a self-contained Linear Congruential Generator
#     (LCG) seeded by SEED, implemented below in pure integer arithmetic. We do
#     NOT use bash's $RANDOM, whose sequence is an implementation detail of bash
#     itself; our LCG is defined entirely by this script, so the byte stream is
#     portable across every environment.
#   * Sizes use an integer "octave" method (no floating point), so there is no
#     dependence on libm/awk float behaviour, which differs across platforms.
#   * The file header records only the invariants (profile, seed, count,
#     params) and NO timestamp, so two files generated with the same inputs are
#     byte-identical and `diff` is a valid reproducibility check. The generation
#     timestamp is printed to stderr instead.
#
# Usage:
#   PROFILE=cpu   COUNT=200 SEED=42 ./shared/gen_targets.sh
#   PROFILE=io    COUNT=200 SEED=42 ./shared/gen_targets.sh
#   PROFILE=mixed COUNT=200 SEED=42 ./shared/gen_targets.sh
#   PROFILE=giants COUNT=8 SEED=42 ./shared/gen_targets.sh
#
#   # write a profile-specific file (recommended for the benchmark matrix):
#   PROFILE=cpu OUT=shared/targets-cpu.txt ./shared/gen_targets.sh
#
# Profiles:
#   cpu    — sizes log-uniform in [MIN_SIZE, MAX_SIZE]; no delays. Isolates the
#            CPU (hash) stage. This is where Go/C++ parallel scaling vs the
#            Python GIL / TS event loop shows up most clearly.
#   io     — fixed small payload (IO_SIZE); a DELAY_FRACTION% subset gets a
#            delay in [MIN_DELAY, MAX_DELAY] ms, rest none. Isolates I/O waiting.
#   mixed  — sizes log-uniform AND a DELAY_FRACTION% subset delayed. Realistic;
#            stresses pool scheduling under variance (head-of-line blocking).
#   giants — GIANTS_COUNT targets of fixed large size GIANTS_SIZE; no delays.
#            Optional contrast experiment to watch the bottleneck shift from
#            concurrency scheduling to memory bandwidth.
#
set -euo pipefail

# ---- help -------------------------------------------------------------------
case "${1:-}" in
  -h|--help)
    sed -n '2,52p' "$0"
    exit 0
    ;;
esac

# ---- configuration (env-driven, with defaults) ------------------------------
PROFILE="${PROFILE:-cpu}"
COUNT="${COUNT:-200}"
SEED="${SEED:-42}"
OUT="${OUT:-shared/targets.txt}"

# size range for cpu/mixed (log-uniform between these, inclusive of the low end)
MIN_SIZE="${MIN_SIZE:-1024}"        # 1 KiB
MAX_SIZE="${MAX_SIZE:-4194304}"     # 4 MiB

# io profile fixed payload size
IO_SIZE="${IO_SIZE:-4096}"          # 4 KiB

# delay parameters for io/mixed
DELAY_FRACTION="${DELAY_FRACTION:-30}"   # percent of targets that get a delay
MIN_DELAY="${MIN_DELAY:-10}"             # ms
MAX_DELAY="${MAX_DELAY:-200}"            # ms

# giants profile
GIANTS_COUNT="${GIANTS_COUNT:-8}"
GIANTS_SIZE="${GIANTS_SIZE:-67108864}"   # 64 MiB

# ---- validation (exit 2 on bad invocation, per project convention) ----------
die() { printf 'gen_targets.sh: %s\n' "$1" >&2; exit 2; }

is_uint() { [[ "$1" =~ ^[0-9]+$ ]]; }

case "$PROFILE" in
  cpu|io|mixed|giants) ;;
  *) die "unknown PROFILE '$PROFILE' (want: cpu|io|mixed|giants)" ;;
esac

for v in COUNT SEED MIN_SIZE MAX_SIZE IO_SIZE DELAY_FRACTION MIN_DELAY MAX_DELAY GIANTS_COUNT GIANTS_SIZE; do
  is_uint "${!v}" || die "$v must be a non-negative integer (got '${!v}')"
done

(( COUNT >= 1 )) || die "COUNT must be >= 1"
(( MIN_SIZE >= 1 )) || die "MIN_SIZE must be >= 1"
(( MAX_SIZE >= MIN_SIZE )) || die "MAX_SIZE ($MAX_SIZE) must be >= MIN_SIZE ($MIN_SIZE)"
(( DELAY_FRACTION <= 100 )) || die "DELAY_FRACTION must be 0..100"
(( MAX_DELAY >= MIN_DELAY )) || die "MAX_DELAY must be >= MIN_DELAY"

# giants uses its own count
if [[ "$PROFILE" == giants ]]; then
  EFFECTIVE_COUNT="$GIANTS_COUNT"
else
  EFFECTIVE_COUNT="$COUNT"
fi

# id width: at least 5 (matches existing convention), wider only if needed so
# that all ids share a width and lexicographic sort == numeric sort.
WIDTH=5
(( ${#EFFECTIVE_COUNT} > WIDTH )) && WIDTH=${#EFFECTIVE_COUNT}

# ---- self-contained RNG (LCG) ----------------------------------------------
# Numerical Recipes constants; 32-bit state. Result is returned in the global
# RAND (NOT via command substitution) so the generator state threads correctly
# through the parent shell. Using $(next_rand) would run it in a subshell and
# LOSE the state update, making every draw identical — so callers MUST invoke
# `next_rand` as a statement and then read $RAND.
#
# Assumes 64-bit shell arithmetic (bash intmax_t) so the multiply does not
# overflow before masking: 1664525 * (2^32-1) ~= 7.1e15 < 2^63. True on all
# 64-bit platforms (i.e. anything you'd run this benchmark on).
_rng_state=$(( SEED & 0xFFFFFFFF ))
RAND=0
next_rand() {
  _rng_state=$(( (1664525 * _rng_state + 1013904223) & 0xFFFFFFFF ))
  # Drop the low bit: an LCG's lowest bits have short periods / low quality.
  RAND=$(( _rng_state >> 1 ))
}

# uniform integer in [0, n) returned in RAND. n must be >= 1.
rand_below() {  # $1 = n
  next_rand
  RAND=$(( RAND % $1 ))
}

# ---- integer log2 floor (for octave bounds) --------------------------------
log2_floor() {  # $1 = n (>=1); echoes floor(log2(n))
  local n=$1 b=0
  while (( n > 1 )); do n=$(( n >> 1 )); b=$(( b + 1 )); done
  printf '%d' "$b"
}

MIN_OCT=$(log2_floor "$MIN_SIZE")
MAX_OCT=$(log2_floor "$MAX_SIZE")
# Select octaves from [MIN_OCT, MAX_OCT). If MAX_SIZE is an exact power of two
# this covers [MIN_SIZE, MAX_SIZE). Guarantee at least one octave.
NUM_OCT=$(( MAX_OCT - MIN_OCT ))
(( NUM_OCT >= 1 )) || NUM_OCT=1

# log-uniform size in [MIN_SIZE, MAX_SIZE), returned in global SIZE.
SIZE=0
log_uniform_size() {
  rand_below "$NUM_OCT"
  local oct=$(( MIN_OCT + RAND ))
  local low=$(( 1 << oct ))
  local high=$(( 1 << (oct + 1) ))
  local span=$(( high - low ))
  rand_below "$span"
  SIZE=$(( low + RAND ))
}

# returns a delay (0 if this target is not in the delayed fraction) in DELAY.
DELAY=0
maybe_delay() {
  rand_below 100
  if (( RAND < DELAY_FRACTION )); then
    rand_below $(( MAX_DELAY - MIN_DELAY + 1 ))
    DELAY=$(( MIN_DELAY + RAND ))
  else
    DELAY=0
  fi
}

# ---- emit one target line for the current profile --------------------------
# printf goes to stdout (redirected to the temp file by the caller). Called as a
# plain statement so RNG globals persist across iterations (no subshell).
emit_line() {  # $1 = id
  local id=$1
  case "$PROFILE" in
    cpu)
      log_uniform_size
      printf '%s?size=%d\n' "$id" "$SIZE"
      ;;
    io)
      maybe_delay
      if (( DELAY > 0 )); then
        printf '%s?size=%d&delay=%d\n' "$id" "$IO_SIZE" "$DELAY"
      else
        printf '%s?size=%d\n' "$id" "$IO_SIZE"
      fi
      ;;
    mixed)
      log_uniform_size
      maybe_delay
      if (( DELAY > 0 )); then
        printf '%s?size=%d&delay=%d\n' "$id" "$SIZE" "$DELAY"
      else
        printf '%s?size=%d\n' "$id" "$SIZE"
      fi
      ;;
    giants)
      printf '%s?size=%d\n' "$id" "$GIANTS_SIZE"
      ;;
  esac
}

# ---- header (invariants only — NO timestamp, to keep files byte-reproducible)
print_header() {
  printf '# polyfetch workload\n'
  printf '# profile=%s seed=%d count=%d\n' "$PROFILE" "$SEED" "$EFFECTIVE_COUNT"
  case "$PROFILE" in
    cpu)
      printf '# size=log-uniform[%d,%d) delay=none\n' "$MIN_SIZE" "$MAX_SIZE" ;;
    io)
      printf '# size=%d delay=%d%% in [%d,%d]ms\n' "$IO_SIZE" "$DELAY_FRACTION" "$MIN_DELAY" "$MAX_DELAY" ;;
    mixed)
      printf '# size=log-uniform[%d,%d) delay=%d%% in [%d,%d]ms\n' "$MIN_SIZE" "$MAX_SIZE" "$DELAY_FRACTION" "$MIN_DELAY" "$MAX_DELAY" ;;
    giants)
      printf '# size=%d delay=none\n' "$GIANTS_SIZE" ;;
  esac
}

# ---- generate (atomic write via temp file) ---------------------------------
mkdir -p "$(dirname "$OUT")"
tmp="$OUT.tmp.$$"
trap 'rm -f "$tmp"' EXIT

{
  print_header
  for (( i = 1; i <= EFFECTIVE_COUNT; i++ )); do
    printf -v id "%0${WIDTH}d" "$i"
    emit_line "$id"
  done
} > "$tmp"

mv "$tmp" "$OUT"
trap - EXIT

# ---- summary to stderr (timestamp lives here, not in the file) -------------
printf 'gen_targets.sh: wrote %s\n' "$OUT" >&2
printf '  profile=%s seed=%d count=%d  (generated %s)\n' \
  "$PROFILE" "$SEED" "$EFFECTIVE_COUNT" "$(date -u +%FT%TZ)" >&2