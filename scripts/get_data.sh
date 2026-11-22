#!/usr/bin/env bash
# Downloads the Silesia corpus and builds the calibration stream used in 3.1.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DATA="$ROOT/data"
mkdir -p "$DATA"

if [ ! -f "$DATA/silesia.zip" ]; then
  echo "downloading silesia.zip ..."
  curl -L --retry 3 -o "$DATA/silesia.zip" \
    https://sun.aei.polsl.pl/~sdeor/corpus/silesia.zip
fi
if [ ! -d "$DATA/silesia" ]; then
  unzip -q "$DATA/silesia.zip" -d "$DATA/silesia"
fi

# Calibration stream: 1 MB from each of the 12 files, in name order.
# Small enough for a full Kmax x j x p grid, still contains 11 file transitions.
if [ ! -f "$DATA/calib.bin" ]; then
  echo "building calib.bin ..."
  : > "$DATA/calib.bin"
  for f in "$DATA"/silesia/*; do
    [ -f "$f" ] || continue
    head -c 1000000 "$f" >> "$DATA/calib.bin"
  done
fi

# Full corpus, single stream (for the final runs and for 3.3).
if [ ! -f "$DATA/all.bin" ]; then
  echo "building all.bin ..."
  cat "$DATA"/silesia/* > "$DATA/all.bin"
fi

ls -l "$DATA/calib.bin" "$DATA/all.bin"
