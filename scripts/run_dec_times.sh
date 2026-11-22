#!/usr/bin/env bash
# Decompression times for the nine best calibration configurations.
# Runs the exact grid winners with --verify, one at a time (isolated), then
# merges one CSV for the report.
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/build/ppm"
DATA="$ROOT/data/calib.bin"
OUT="$ROOT/results"
LOG="$OUT/dec_times_seq.log"
mkdir -p "$OUT"
exec >>"$LOG" 2>&1
echo "=== sequential dec times started $(date -Is) ==="

specs=(
  "1 30 none 1000 5"
  "1 17 reset 50000 30"
  "1 17 prune 50000 15"
  "4 7 none 1000 5"
  "4 8 reset 10000 50"
  "4 8 prune 10000 40"
  "8 5 none 1000 5"
  "8 5 reset 10000 50"
  "8 5 prune 5000 40"
)

merged="$OUT/.31_dec_times_seq.$$.csv"
echo "file,bits,kmax,mode,j,p,mem_cap,orig_bytes,symbols,payload_bytes,bits_per_symbol,bits_per_byte,enc_s,dec_s,resets,prunes,verify" > "$merged"
fail=0
for spec in "${specs[@]}"; do
  set -- $spec
  bits=$1; kmax=$2; mode=$3; j=$4; p=$5
  tmp="$OUT/.dec_seq_A${bits}_${mode}.$$.csv"
  echo "=== A${bits} ${mode} Kmax=${kmax} j=${j} p=${p} $(date -Is)"
  nice -n 10 "$BIN" bench --files "$DATA" --bits "$bits" --kmax "$kmax" \
    --j "$j" --p "$p" --modes "$mode" --threads 1 --mem 8000000 \
    --out "$tmp" --verify
  rows=$(( $(wc -l < "$tmp" 2>/dev/null || echo 1) - 1 ))
  ok=$(tail -n 1 "$tmp" 2>/dev/null | awk -F, '{print $NF}')
  if [ "$rows" -eq 1 ] && [ "$ok" = "ok" ]; then
    tail -n 1 "$tmp" >> "$merged"
  else
    echo "FAILED A${bits} ${mode} rows=${rows} verify=${ok}"
    fail=1
  fi
done

if [ "$fail" -eq 0 ]; then
  mv "$merged" "$OUT/31_dec_times.csv"
  echo "wrote $OUT/31_dec_times.csv"
  printf 'done\n' > "$OUT/DEC_TIMES_DONE"
else
  echo "not publishing partial table"
  printf 'failed\n' > "$OUT/DEC_TIMES_FAILED"
fi
echo "=== sequential dec times done $(date -Is) ==="
