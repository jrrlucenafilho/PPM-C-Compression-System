#!/usr/bin/env python3
"""Pick one (Kmax, j, p) per alphabet from the completed 3.1 sweep.

Selection rule: among reset-mode rows, take the best bits/byte and then the
smallest Kmax within 0.3% of that value (avoids paying for a flat tail).
The same Kmax/j/p is used for all three modes in the full-corpus run so the
3.3 trace comparison isolates the effect of the mode.

Prints shell assignments, e.g.  B1=17 J1=50000 P1=30 ...
"""
import csv
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RES = os.path.join(ROOT, "results")


def load(bits):
    """Read and cast the completed 3.1 grid rows for alphabet `bits`; returns
    an empty list when the CSV is missing or a row is malformed."""
    path = os.path.join(RES, f"31_A{bits}.csv")
    rows = []
    if not os.path.exists(path):
        return rows
    with open(path, newline="") as f:
        for r in csv.DictReader(f):
            if not r.get("payload_bytes"):
                continue
            try:
                r["kmax"] = int(r["kmax"])
                r["j"] = int(r["j"])
                r["p"] = int(r["p"])
                r["bits_per_byte"] = float(r["bits_per_byte"])
            except (TypeError, ValueError):
                continue
            rows.append(r)
    return rows


def choose(rows, max_kmax):
    """Return the reset-mode row with the best bits/byte whose Kmax is within
    0.3% of that best (smallest Kmax wins); falls back to any mode, or None."""
    sel = [r for r in rows if r["mode"] == "reset" and r["kmax"] <= max_kmax]
    if not sel:
        sel = [r for r in rows if r["kmax"] <= max_kmax]
    if not sel:
        return None
    best = min(r["bits_per_byte"] for r in sel)
    near = [r for r in sel if r["bits_per_byte"] <= best * 1.003]
    return min(near, key=lambda r: (r["kmax"], r["bits_per_byte"]))


def main():
    """Print shell assignments (`export B{bits}=... J{bits}=... P{bits}=...`)
    for the chosen configs; returns 0 on success, 1 if an alphabet has no rows."""
    out = []
    ok = True
    for bits, max_kmax in ((1, 20), (4, 8), (8, 8)):
        rows = load(bits)
        c = choose(rows, max_kmax)
        if not c:
            print(f"# no usable rows for A{bits}", file=sys.stderr)
            ok = False
            continue
        out.append((bits, c))
        print(f"# A{bits}: Kmax={c['kmax']} j={c['j']} p={c['p']} "
              f"bpb={c['bits_per_byte']:.4f}", file=sys.stderr)
    if not ok:
        return 1
    for bits, c in out:
        print(f"export B{bits}={c['kmax']} J{bits}={c['j']} P{bits}={c['p']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
