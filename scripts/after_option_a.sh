#!/usr/bin/env bash
# Wait for Option A, then rerun the traces with emitted bits and refresh figures.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/results"
LOG="$OUT/after_option_a.log"
exec >>"$LOG" 2>&1
echo "=== after-Option-A watcher started $(date -Is) ==="
while [ ! -f "$OUT/OPTION_A_DONE" ] && [ ! -f "$OUT/OPTION_A_FAILED" ]; do
  sleep 60
done
if [ -f "$OUT/OPTION_A_FAILED" ]; then
  echo "Option A failed; not running traces"
  exit 1
fi
echo "Option A done; starting actual-bit traces $(date -Is)"
"$ROOT/scripts/run_actual_bit_traces.sh"
if [ -f "$OUT/ACTUAL_TRACES_FAILED" ]; then
  echo "traces failed"
  exit 1
fi
python3 "$ROOT/scripts/analyze_traces.py" > "$OUT/actual_trace_stats.txt"
python3 "$ROOT/scripts/plots.py" > "$OUT/plots_actual.log" 2>&1
echo "=== after-Option-A watcher done $(date -Is) ==="
printf 'done\n' > "$OUT/ACTUAL_WORK_DONE"
