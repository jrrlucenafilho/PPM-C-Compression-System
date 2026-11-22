#!/usr/bin/env bash
# Run the A1 full-corpus finals while the A1 grid is still finishing.
# One thread, low priority; writes temp files and renames only on success.
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/build/ppm"
DATA="$ROOT/data/all.bin"
OUT="$ROOT/results"
LOG="$OUT/prefinal_A1.log"
mkdir -p "$OUT"
exec >>"$LOG" 2>&1
echo "=== prefinal A1 started $(date -Is) ==="

for mode in none reset prune; do
  out="$OUT/final_A1_${mode}.csv"
  if [ -s "$out" ]; then
    echo "skip A1 ${mode} (exists)"
    continue
  fi
  tmp="$OUT/.prefinal_A1_${mode}.$$.csv"
  echo "=== A1 Kmax=17 mode=${mode} j=50000 p=30 $(date -Is)"
  "$BIN" bench --files "$DATA" --bits 1 --kmax 17 \
    --j 50000 --p 30 --modes "$mode" --threads 1 --mem 20000000 \
    --out "$tmp" --trace-prefix "$OUT/trace_silesia" --trace-interval 50000 \
    --verify
  rc=$?
  if [ $rc -eq 0 ] && [ -s "$tmp" ] && [ $(( $(wc -l < "$tmp") - 1 )) -eq 1 ]; then
    mv "$tmp" "$out"
    echo "wrote $out $(date -Is)"
  else
    echo "FAILED A1 ${mode} rc=$rc; leaving $tmp for inspection"
  fi
done

echo "=== prefinal A1 done $(date -Is) ==="
printf 'done\n' > "$OUT/PREFINAL_A1_DONE"
