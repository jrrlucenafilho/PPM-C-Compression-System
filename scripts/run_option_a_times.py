#!/usr/bin/env python3
"""Option A: compression/decompression time for the best (j,p) at every Kmax.

For each alphabet and Kmax, pick the grid row with the smallest bits/byte for
each mode, rerun that exact configuration on the 12 MB calibration stream with
--verify (sequential, one thread), and append the measured row to
results/31_times_by_kmax.csv. Resumable: rows already present are skipped.
"""
import csv
import os
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, "build", "ppm")
DATA = os.path.join(ROOT, "data", "calib.bin")
OUT = os.path.join(ROOT, "results")
MERGED = os.path.join(OUT, "31_times_by_kmax.csv")
LOG = os.path.join(OUT, "optA.log")
HEADER = ("file,bits,kmax,mode,j,p,mem_cap,orig_bytes,symbols,payload_bytes,"
          "bits_per_symbol,bits_per_byte,enc_s,dec_s,resets,prunes,verify\n")
MODES = ("none", "reset", "prune")
KMAX = {1: 32, 4: 8, 8: 8}


def load_grid(bits):
    path = os.path.join(OUT, f"31_A{bits}.csv")
    groups = {}
    with open(path, newline="") as f:
        for r in csv.DictReader(f):
            if not r.get("payload_bytes"):
                continue
            key = (int(r["kmax"]), r["mode"])
            if key not in groups or float(r["bits_per_byte"]) < float(groups[key]["bits_per_byte"]):
                groups[key] = r
    return groups


def specs():
    for bits in (1, 4, 8):
        g = load_grid(bits)
        for k in range(KMAX[bits] + 1):
            for mode in MODES:
                r = g.get((k, mode))
                if not r:
                    raise SystemExit(f"missing grid row bits={bits} kmax={k} mode={mode}")
                yield bits, k, mode, int(r["j"]), int(r["p"])


def existing_keys():
    keys = set()
    if not os.path.exists(MERGED):
        return keys
    with open(MERGED, newline="") as f:
        for r in csv.DictReader(f):
            keys.add((int(r["bits"]), int(r["kmax"]), r["mode"]))
    return keys


def main():
    todo = [s for s in specs() if (s[0], s[1], s[2]) not in existing_keys()]
    total = len(list(specs()))
    print(f"Option A: {total} total, {len(todo)} remaining", flush=True)
    if not os.path.exists(MERGED) or os.path.getsize(MERGED) == 0:
        with open(MERGED, "w") as f:
            f.write(HEADER)
    with open(LOG, "a") as log:
        log.write(f"=== Option A start {time.strftime('%Y-%m-%d %H:%M:%S')} "
                  f"({len(todo)} jobs) ===\n")
        log.flush()
        fails = 0
        for n, (bits, kmax, mode, j, p) in enumerate(todo, 1):
            tmp = os.path.join(OUT, f".optA_{bits}_{kmax}_{mode}_{os.getpid()}.csv")
            cmd = ["nice", "-n", "10", BIN, "bench", "--files", DATA,
                   "--bits", str(bits), "--kmax", str(kmax), "--j", str(j),
                   "--p", str(p), "--modes", mode, "--threads", "1",
                   "--mem", "8000000", "--out", tmp, "--verify"]
            t0 = time.time()
            res = subprocess.run(cmd, capture_output=True, text=True)
            dt = time.time() - t0
            row = ""
            if os.path.exists(tmp):
                with open(tmp, newline="") as f:
                    rows = list(csv.reader(f))
                if len(rows) >= 2:
                    row = ",".join(rows[-1])
            ok = row.endswith(",ok")
            if res.returncode == 0 and ok:
                with open(MERGED, "a") as f:
                    f.write(row + "\n")
                msg = f"[{n}/{len(todo)}] A{bits} K={kmax} {mode} j={j} p={p} ok {dt:.1f}s"
            else:
                fails += 1
                msg = (f"[{n}/{len(todo)}] A{bits} K={kmax} {mode} FAILED "
                       f"rc={res.returncode} {res.stderr[-200:]}")
            print(msg, flush=True)
            log.write(msg + "\n")
            log.flush()
        log.write(f"=== Option A done {time.strftime('%Y-%m-%d %H:%M:%S')} ===\n")
    if fails:
        with open(os.path.join(OUT, "OPTION_A_FAILED"), "w") as f:
            f.write(f"{fails} failures\n")
        print(f"Option A finished with {fails} failures", flush=True)
    else:
        with open(os.path.join(OUT, "OPTION_A_DONE"), "w") as f:
            f.write("done\n")
        print("Option A complete", flush=True)


if __name__ == "__main__":
    sys.exit(main())
