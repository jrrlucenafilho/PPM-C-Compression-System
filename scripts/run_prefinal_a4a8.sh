#!/usr/bin/env bash
# Run the A4/A8 full-corpus finals while the A1 grid is still finishing.
# Uses one thread, low priority, and atomic-ish temp files so the auto_final
# watcher can safely skip these configurations later.
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/build/ppm"
DATA="$ROOT/data/all.bin"
OUT="$ROOT/results"
LOG="$OUT/prefinal_A4A8.log"
mkdir -p "$OUT"
exec >>"$LOG" 2>&1
echo "=== prefinal started $(date -Is) ==="

run() {
  local bits=$1 kmax=$2 j=$3 p=$4 mode=$5
  local out="$OUT/final_A${bits}_${mode}.csv"
  if [ -s "$out" ]; then
    echo "skip A${bits} ${mode} (exists)"
    return
  fi
  local tmp="$OUT/.prefinal_A${bits}_${mode}.$$.csv"
  echo "=== A${bits} Kmax=${kmax} mode=${mode} j=${j} p=${p} $(date -Is)"
  "$BIN" bench --files "$DATA" --bits "$bits" --kmax "$kmax" \
    --j "$j" --p "$p" --modes "$mode" --threads 1 --mem 20000000 \
    --out "$tmp" --trace-prefix "$OUT/trace_silesia" --trace-interval 50000 \
    --verify
  local rc=$?
  if [ $rc -eq 0 ] && [ -s "$tmp" ] && [ $(( $(wc -l < "$tmp") - 1 )) -eq 1 ]; then
    mv "$tmp" "$out"
    echo "wrote $out $(date -Is)"
  else
    echo "FAILED A${bits} ${mode} rc=$rc; leaving $tmp for inspection"
  fi
}

run 4 8 10000 50 none
run 4 8 10000 50 reset
run 4 8 10000 50 prune
run 8 5 10000 50 none
run 8 5 10000 50 reset
run 8 5 10000 50 prune

echo "=== prefinal done $(date -Is) ==="
printf 'done\n' > "$OUT/PREFINAL_A4A8_DONE"
