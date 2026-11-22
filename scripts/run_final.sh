#!/usr/bin/env bash
# Final runs: best configuration per alphabet on the whole Silesia corpus
# (single concatenated stream), plus progressive traces for 3.2 and 3.3,
# plus the 7-Zip PPMd comparison.
#
# Set the tuned values before running, e.g.:
#   B1=6 J1=50000 P1=20 B4=4 J4=50000 P4=20 B8=6 J8=50000 P8=20 ./scripts/run_final.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/build/ppm"
DATA="$ROOT/data"
OUT="${OUT:-$ROOT/results}"
mkdir -p "$OUT"

B1="${B1:-6}"; J1="${J1:-50000}"; P1="${P1:-20}"
B4="${B4:-4}"; J4="${J4:-50000}"; P4="${P4:-20}"
B8="${B8:-6}"; J8="${J8:-50000}"; P8="${P8:-20}"
MEM_FINAL="${MEM_FINAL:-20000000}"
VERIFY="${VERIFY:-1}"

if [ ! -f "$DATA/all.bin" ]; then "$ROOT/scripts/get_data.sh"; fi

for spec in "1 $B1 $J1 $P1" "4 $B4 $J4 $P4" "8 $B8 $J8 $P8"; do
  set -- $spec
  bits=$1; kmax=$2; j=$3; p=$4
  for mode in none reset prune; do
    out="$OUT/final_A${bits}_${mode}.csv"
    [ -s "$out" ] && { echo "skip $out"; continue; }
    echo "=== final A${bits} Kmax=${kmax} mode=${mode} j=${j} p=${p}"
    extra=()
    [ "$VERIFY" = "1" ] && extra+=(--verify)
    # One pass on the concatenated corpus also writes the 3.3 progressive
    # trace, so we do not compress the whole corpus twice.
    "$BIN" bench --files "$DATA/all.bin" --bits "$bits" --kmax "$kmax" \
      --j "$j" --p "$p" --modes "$mode" --threads 1 --mem "$MEM_FINAL" \
      --out "$out" --trace-prefix "$OUT/trace_silesia" --trace-interval 50000 \
      "${extra[@]}"
  done
done

# Progressive average length on Dickens (3.2), one trace per alphabet.
for bits in 1 4 8; do
  case $bits in 1) k=$B1;; 4) k=$B4;; 8) k=$B8;; esac
  tr="$OUT/trace_dickens_A${bits}.csv"
  [ -s "$tr" ] || "$BIN" c "$DATA/silesia/dickens" /tmp/dickens_A${bits}.ppm \
      --bits "$bits" --kmax "$k" --mode none --trace "$tr" --interval 10000
done

# Commercial reference (7-Zip PPMd and LZMA2).
"$ROOT/scripts/run_commercial.sh"

echo "done. results in $OUT"
