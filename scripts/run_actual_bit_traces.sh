#!/usr/bin/env bash
# Rerun only the progressive traces, now recording the arithmetic coder's
# actually emitted bits (the monitor's estimated cost is still kept as extra
# columns). Does not touch the grid or the final CSVs.
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/build/ppm"
OUT="$ROOT/results"
ALL="$ROOT/data/all.bin"
DICKENS="$ROOT/data/silesia/dickens"
LOG="$OUT/actual_traces.log"
mkdir -p "$OUT"
exec >>"$LOG" 2>&1
echo "=== actual-bit traces start $(date -Is) ==="

pids=()
fail=0

# Compress data/all.bin with $1=bits, $2=Kmax, $3=j, $4=p, $5=mode and write
# the progressive trace to results/trace_silesia_A{bits}_{mode}.csv.
run_silesia() {
  local bits=$1 kmax=$2 j=$3 p=$4 mode=$5
  local trace="$OUT/trace_silesia_A${bits}_${mode}.csv"
  local ppm="/tmp/actual_silesia_A${bits}_${mode}.ppm"
  nice -n 10 "$BIN" c "$ALL" "$ppm" --bits "$bits" --kmax "$kmax" \
    --j "$j" --p "$p" --mode "$mode" --mem 20000000 \
    --trace "$trace" --interval 50000
  echo "A${bits} ${mode} -> $trace rc=$?"
}

for spec in "1 17 50000 30" "4 8 10000 50" "8 5 10000 50"; do
  set -- $spec
  for mode in none reset prune; do
    run_silesia "$1" "$2" "$3" "$4" "$mode" &
    pids+=($!)
  done
done
for pid in "${pids[@]}"; do wait "$pid" || fail=1; done

for bits in 1 4 8; do
  case $bits in
    1) k=17; j=50000; p=30;;
    4) k=8; j=10000; p=50;;
    8) k=5; j=10000; p=50;;
  esac
  trace="$OUT/trace_dickens_A${bits}.csv"
  nice -n 10 "$BIN" c "$DICKENS" "/tmp/actual_dickens_A${bits}.ppm" \
    --bits "$bits" --kmax "$k" --j "$j" --p "$p" --mode none \
    --mem 20000000 --trace "$trace" --interval 10000
  echo "Dickens A${bits} -> $trace rc=$?"
done

if [ "$fail" -eq 0 ]; then
  echo "=== actual-bit traces done $(date -Is) ==="
  printf 'done\n' > "$OUT/ACTUAL_TRACES_DONE"
else
  echo "=== actual-bit traces finished with failures $(date -Is) ==="
  printf 'failed\n' > "$OUT/ACTUAL_TRACES_FAILED"
fi
