#!/usr/bin/env bash
# Waits for the three detached 3.1 sweeps to finish, then runs the full-corpus
# finals, traces, tables and figures automatically. Never signals the sweeps.
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/results"
LOG="$OUT/auto_final.log"
mkdir -p "$OUT"
exec >>"$LOG" 2>&1

echo "=== watcher started $(date -Is) ==="
printf 'watching\n' > "$OUT/AUTO_FINAL_WATCHING"

alive() {
  [ -r "/proc/$1/cmdline" ] || return 1
  tr '\0' ' ' < "/proc/$1/cmdline" | grep -q 'build/ppm bench'
}

for pid in "$@"; do
  echo "waiting for pid $pid"
  while alive "$pid"; do sleep 60; done
  echo "pid $pid exited at $(date -Is)"
done

a1=$(( $(wc -l < "$OUT/31_A1.csv") - 1 ))
a4=$(( $(wc -l < "$OUT/31_A4.csv") - 1 ))
a8=$(( $(wc -l < "$OUT/31_A8.csv") - 1 ))
echo "rows: A1=$a1 A4=$a4 A8=$a8"
if [ "$a1" -lt 3201 ] || [ "$a4" -lt 873 ] || [ "$a8" -lt 873 ]; then
  echo "incomplete sweep; not starting finals"
  printf 'incomplete A1=%s A4=%s A8=%s\n' "$a1" "$a4" "$a8" > "$OUT/AUTO_FINAL_INCOMPLETE"
  exit 1
fi

eval "$(python3 "$ROOT/scripts/pick_config.py")" || {
  echo "could not pick configs"; printf 'pick failed\n' > "$OUT/AUTO_FINAL_INCOMPLETE"; exit 1;
}
echo "chosen: B1=$B1 J1=$J1 P1=$P1 | B4=$B4 J4=$J4 P4=$P4 | B8=$B8 J8=$J8 P8=$P8"

# One full pass per (alphabet, mode) writes both the final CSV and the 3.3 trace.
VERIFY=1 MEM_FINAL=20000000 "$ROOT/scripts/run_final.sh"
if [ $? -ne 0 ]; then
  echo "run_final failed"
  printf 'run_final failed\n' > "$OUT/AUTO_FINAL_INCOMPLETE"
  exit 1
fi

python3 "$ROOT/scripts/latex_tables.py"
python3 "$ROOT/scripts/plots.py"
python3 "$ROOT/scripts/summarize.py" > "$OUT/tables_31.md"
( cd "$ROOT/report" && pdflatex -interaction=nonstopmode main.tex >/dev/null 2>&1 && \
  pdflatex -interaction=nonstopmode main.tex >/dev/null 2>&1 )

echo "=== watcher done $(date -Is) ==="
printf 'done\n' > "$OUT/AUTO_FINAL_DONE"
