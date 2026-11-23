# Small Dickens subset application

- `dickens_small.txt` — Chapter I of *A Child's History of England* (Charles Dickens)

Run the commands below from the repository root (the directory containing
`build/` and `showcase/`). If the binary is missing, build it first:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## 1. Compress

```bash
./build/ppm c showcase/dickens_small.txt showcase/dickens_small.ppm \
    --bits 8 --kmax 5 --mode none
```

Expected output (times vary slightly between runs):

```text
bits=8 kmax=5 mode=none j=50000 p=20
in=14160B out=5351B payload=5319B symbols=14160
bits/symbol=3.00508 bits/byte=3.00508 resets=0 prunes=0 time=0.00692643s (2.04434 Msym/s)
```

What to point out:

- `in` / `out` — original and compressed file sizes (the compressed file has a
  32-byte header; `payload` is the arithmetic-coded data alone).
- `bits/symbol` — final average length, the metric requested in the specification.
- `bits/byte` — same value for A8; for A1 it is 8× the bits/symbol.
- `time` — compression wall time and throughput.
- `resets` / `prunes` — adaptation counters (both zero here because
  `--mode none` disables monitoring).

## 2. Decompress

```bash
./build/ppm d showcase/dickens_small.ppm showcase/dickens_small.out
```

Expected output:

```text
out=14160B time=0.00638448s
```

## 3. Check binary integrity

```bash
cmp showcase/dickens_small.txt showcase/dickens_small.out && echo OK
```

Expected output:

```text
OK
```

`cmp` prints nothing when the files are identical; `OK` confirms success. This
is the binary comparison required by the specification.

## 4. Show the performance monitor (windows, reset/prune, decoder sync)

`--monitor-log` prints every full `j`-symbol window: position `n`, the window's
estimated average, the previous window and percentage change, and any reset or
prune trigger. It works in both `c` and `d`, so the decoder can be shown making
the same decisions:

```bash
./build/ppm c showcase/dickens_small.txt showcase/dickens_small_monitor.ppm \
    --bits 8 --kmax 5 --mode reset --j 1000 --p 5 --monitor-log

./build/ppm d showcase/dickens_small_monitor.ppm showcase/dickens_small_monitor.out \
    --monitor-log

cmp showcase/dickens_small.txt showcase/dickens_small_monitor.out && echo OK
```

Expected monitor lines (deterministic; the normal stats lines are omitted):

```text
[monitor] enabled: window j=1000 symbols, p=5%, action=reset (estimated bits/symbol per window)
[monitor] n=1000 window=1 avg=3.1715 bits/sym (baseline)
...
[monitor] n=8000 window=8 avg=2.3564 bits/sym prev=2.1859 (+7.80%) > p=5% -> RESET
...
[monitor] n=13000 window=13 avg=2.4119 bits/sym prev=2.1782 (+10.73%) > p=5% -> RESET
[monitor] n=14000 window=14 avg=3.0382 bits/sym (baseline)
[monitor] done: windows=14 resets=2 prunes=0
[monitor] final partial window: 160 symbols (not compared)
OK
```

To prove the decompressor reaches the same decisions, capture both logs and
compare only the monitor lines:

```bash
./build/ppm c showcase/dickens_small.txt showcase/dickens_small_monitor.ppm \
    --bits 8 --kmax 5 --mode reset --j 1000 --p 5 --monitor-log \
    2> showcase/monitor_c.log

./build/ppm d showcase/dickens_small_monitor.ppm showcase/dickens_small_monitor.out \
    --monitor-log 2> showcase/monitor_d.log

grep '^\[monitor\]' showcase/monitor_c.log > showcase/monitor_c.txt
grep '^\[monitor\]' showcase/monitor_d.log > showcase/monitor_d.txt
diff showcase/monitor_c.txt showcase/monitor_d.txt && echo "same monitor decisions"
```

Expected output: `same monitor decisions`.

## Optional: progressive average-length data

The 3.2-style curve is written to a CSV, not printed to the terminal:

```bash
./build/ppm c showcase/dickens_small.txt showcase/dickens_small_trace.ppm \
    --bits 8 --kmax 5 --mode none \
    --trace showcase/trace_small.csv --interval 1000
head -5 showcase/trace_small.csv
```

Columns: `n,bits,avg,ideal_bits,ideal_avg`, where `avg = bits/n`.
