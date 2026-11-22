#!/usr/bin/env bash
# Section 3.1: Kmax x j x p x mode sweep on the calibration stream.
#
# Default grid (about 1-2 h on 12 threads, 12 MB stream):
#   A1: Kmax 0..32, A4/A8: Kmax 0..8
#   j in {10000,50000,100000}, p in {10,20,30,50}
#   modes none/reset/prune
#
# Environment:
#   THREADS=12  MEM=8000000  OUT=results
#   J_LIST=...  P_LIST=...  QUICK=1  FORCE=1  RESUME=1
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/build/ppm"
DATA="$ROOT/data"
OUT="${OUT:-$ROOT/results}"
THREADS="${THREADS:-12}"
MEM="${MEM:-8000000}"

if [ "${QUICK:-0}" = "1" ]; then
  J_LIST="${J_LIST:-10000,50000}"
  P_LIST="${P_LIST:-10,20,30}"
else
  J_LIST="${J_LIST:-10000,50000,100000}"
  P_LIST="${P_LIST:-10,20,30,50}"
fi

if [ ! -x "$BIN" ]; then
  cmake -S "$ROOT" -B "$ROOT/build" -DCMAKE_BUILD_TYPE=Release
  cmake --build "$ROOT/build" -j
fi
if [ ! -f "$DATA/calib.bin" ]; then
  "$ROOT/scripts/get_data.sh"
fi
mkdir -p "$OUT"

run() {
  local bits="$1" kmax="$2" out="$3"
  if [ -s "$out" ] && [ "${FORCE:-0}" != "1" ] && [ "${RESUME:-0}" != "1" ]; then
    echo "skip $out (already exists)"
    return
  fi
  echo "=== alphabet ${bits} bits, Kmax ${kmax} -> $out"
  local extra=()
  [ "${RESUME:-0}" = "1" ] && extra+=(--resume)
  "$BIN" bench --files "$DATA/calib.bin" \
    --bits "$bits" --kmax "$kmax" \
    --j "$J_LIST" --p "$P_LIST" --modes none,reset,prune \
    --threads "$THREADS" --mem "$MEM" --out "$out" "${extra[@]}"
}

run 1 0:32 "$OUT/31_A1.csv"
run 4 0:8  "$OUT/31_A4.csv"
run 8 0:8  "$OUT/31_A8.csv"

echo "done. results in $OUT"
