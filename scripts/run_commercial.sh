#!/usr/bin/env bash
# Commercial reference: 7-Zip PPMd (orders 6 and 16) and LZMA2, one thread.
# Writes results/commercial.csv atomically; skips if it already exists.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DATA="$ROOT/data"
OUT="${OUT:-$ROOT/results}"
CSV="$OUT/commercial.csv"
mkdir -p "$OUT"

if ! command -v 7z >/dev/null 2>&1; then
  echo "7z not found; skipping commercial comparison" >&2
  exit 0
fi
if [ -s "$CSV" ]; then
  echo "skip $CSV (already exists)"
  exit 0
fi

TMP="$OUT/.commercial.$$.csv"
echo "tool,file,method,out_bytes,bits_per_byte" > "$TMP"
for f in "$DATA/all.bin" "$DATA/silesia/dickens"; do
  [ -f "$f" ] || continue
  for spec in "PPMd:o=6:mem=256m" "PPMd:o=16:mem=256m" "LZMA2"; do
    rm -f /tmp/ref.7z
    7z a -t7z -m0="$spec" -mx9 -mmt=1 -y /tmp/ref.7z "$f" >/dev/null
    sz=$(stat -c%s /tmp/ref.7z)
    orig=$(stat -c%s "$f")
    bb=$(awk -v s="$sz" -v o="$orig" 'BEGIN{printf "%.4f", s*8/o}')
    echo "7z,$f,$spec,$sz,$bb" >> "$TMP"
    echo "$(basename "$f") $spec -> $sz bytes ($bb bits/byte)" >&2
  done
done
mv "$TMP" "$CSV"
echo "wrote $CSV"
