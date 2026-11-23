#!/usr/bin/env python3
"""Print the numbers used in report sections 3.2 and 3.3 from the trace CSVs.

The trace format is n,bits,avg,ideal_bits,ideal_avg; `avg` is the number of
bits actually emitted divided by n.
"""
import csv
import os
import statistics

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RES = os.path.join(ROOT, "results")


def load(path):
    """Read a trace CSV (`n,bits,avg,...`) and return (positions, averages)
    as two parallel lists: symbol counts and accumulated bits per symbol."""
    ns, av = [], []
    with open(path, newline="") as f:
        for r in csv.DictReader(f):
            ns.append(int(r["n"]))
            av.append(float(r["avg"]))
    return ns, av


def dickens():
    """Print final bits/byte for the Dickens trace plus the symbol count and
    corpus percentage needed to get within 5% and 1% of it (A1, A4, A8)."""
    print("=== Dickens (bits/byte) ===")
    total = 10192446
    for bits in (1, 4, 8):
        ns, av = load(os.path.join(RES, f"trace_dickens_A{bits}.csv"))
        scale = 8.0 / bits
        bpb = [a * scale for a in av]
        final = bpb[-1]
        def point(tol):
            """Return (symbol count, corpus %) of the first trace point from
            which bits/byte stays within `tol` of the final value."""
            outside = [i for i, v in enumerate(bpb) if abs(v - final) / final > tol]
            i = outside[-1] + 1 if outside else 0
            return ns[i], 100.0 * ns[i] * bits / 8.0 / total
        n5, p5 = point(0.05)
        n1, p1 = point(0.01)
        print(f"A{bits}: final={final:.4f} bpb; 5% at {n5} symbols "
              f"({p5:.1f}% = {n5*bits/8.0/1e6:.2f} MB); "
              f"1% at {n1} ({p1:.1f}% = {n1*bits/8.0/1e6:.2f} MB)")


def silesia():
    """Print final bits/byte, mean boundary ratio and recovery time for every
    alphabet/mode on the concatenated Silesia traces (reads data/silesia for
    the per-file sizes that define the boundaries)."""
    files = sorted(f for f in os.listdir(os.path.join(ROOT, "data", "silesia"))
                   if os.path.isfile(os.path.join(ROOT, "data", "silesia", f)))
    sizes = [os.path.getsize(os.path.join(ROOT, "data", "silesia", f)) for f in files]
    cum, total = [], 0
    for s in sizes[:-1]:
        total += s
        cum.append(total)

    print("\n=== Silesia boundary and recovery stats ===")
    for bits in (1, 4, 8):
        scale = 8.0 / bits
        bounds = [b * 8.0 / bits for b in cum]
        for mode in ("none", "reset", "prune"):
            ns, av = load(os.path.join(RES, f"trace_silesia_A{bits}_{mode}.csv"))
            local = []
            for i in range(1, len(ns)):
                dn = ns[i] - ns[i - 1]
                if dn > 0:
                    local.append((ns[i - 1], ns[i],
                                  (av[i] * ns[i] - av[i - 1] * ns[i - 1]) / dn))
            W = 2_000_000 * 8.0 / bits
            ratios = []
            for b in bounds:
                pre = [c for (a, z, c) in local if z <= b and a >= b - W]
                post = [c for (a, z, c) in local if a >= b and z <= b + W]
                if pre and post:
                    ratios.append((sum(post) / len(post)) / (sum(pre) / len(pre)))
            Wr = 1_000_000 * 8.0 / bits
            rec = []
            for b in bounds:
                pre = [c for (a, z, c) in local if z <= b and a >= b - Wr]
                if not pre:
                    continue
                target = 1.10 * (sum(pre) / len(pre))
                for (a, z, c) in local:
                    if a >= b and c <= target and z > a:
                        rec.append(z - b)
                        break
            final = av[-1] * scale
            print(f"A{bits} {mode:5s}: final={final:.4f} bpb; "
                  f"boundary ratio mean={statistics.mean(ratios):.4f}; "
                  f"recovery median={statistics.median(rec)/1e6:.3f}M "
                  f"mean={statistics.mean(rec)/1e6:.3f}M")


if __name__ == "__main__":
    dickens()
    silesia()
