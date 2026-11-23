#!/usr/bin/env python3
"""Plots for the PPM-C report.

Reads results/31_A*.csv produced by scripts/run31.sh and the trace files
produced by scripts/run_final.sh.  Writes PNGs into results/figs/.

    python3 scripts/plots.py
"""
import csv
import os
from collections import defaultdict

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RES = os.path.join(ROOT, "results")
FIGS = os.path.join(RES, "figs")
os.makedirs(FIGS, exist_ok=True)

MODES = ("none", "reset", "prune")
COLORS = {"none": "#444444", "reset": "#d62728", "prune": "#1f77b4"}


def load(path):
    """Load a 3.1 results CSV and cast its numeric columns; returns a list of
    row dicts (one per completed bench job)."""
    rows = []
    with open(path, newline="") as f:
        for r in csv.DictReader(f):
            for k in ("bits", "kmax", "j", "p"):
                r[k] = int(r[k])
            for k in ("bits_per_symbol", "bits_per_byte", "enc_s"):
                r[k] = float(r[k])
            rows.append(r)
    return rows


def best_by_kmax(rows, mode):
    """Minimum bits/byte over (j,p) for each Kmax."""
    best = {}
    for r in rows:
        if r["mode"] != mode:
            continue
        k = r["kmax"]
        if k not in best or r["bits_per_byte"] < best[k]:
            best[k] = r["bits_per_byte"]
    return sorted(best.items())


def plot_kmax(rows, bits):
    """Save figs/bpb_vs_kmax_A{bits}.png: best bits/byte per Kmax and mode,
    using the best (j,p) of each mode."""
    plt.figure(figsize=(7, 4))
    for mode in MODES:
        pts = best_by_kmax(rows, mode)
        if not pts:
            continue
        xs, ys = zip(*pts)
        plt.plot(xs, ys, marker="o", ms=3, label=mode, color=COLORS[mode])
    plt.xlabel("Kmax (ordem maxima)")
    plt.ylabel("bits/byte (melhor j,p)")
    plt.title(f"Alfabeto A{bits}: bits/byte vs Kmax")
    plt.grid(alpha=.3)
    plt.legend()
    out = os.path.join(FIGS, f"bpb_vs_kmax_A{bits}.png")
    plt.tight_layout()
    plt.savefig(out, dpi=150)
    plt.close()
    print("wrote", out)


def best_kmax(rows, mode):
    """Return the Kmax with the lowest bits/byte for `mode`, or None when the
    mode has no rows."""
    pts = best_by_kmax(rows, mode)
    if not pts:
        return None
    return min(pts, key=lambda kv: kv[1])[0]


def plot_param(rows, bits, mode, param):
    """Save figs/bpb_vs_{param}_A{bits}_{mode}.png: bits/byte against `param`
    (`j` or `p`) at the mode's best Kmax, one line per value of the other knob.
    Does nothing when the mode has no rows."""
    k = best_kmax(rows, mode)
    if k is None:
        return
    sel = [r for r in rows if r["mode"] == mode and r["kmax"] == k]
    plt.figure(figsize=(7, 4))
    other = "p" if param == "j" else "j"
    for val in sorted({r[other] for r in sel}):
        pts = sorted((r[param], r["bits_per_byte"]) for r in sel if r[other] == val)
        if len(pts) < 2 and param == "j":
            pass
        xs, ys = zip(*pts)
        plt.plot(xs, ys, marker="o", ms=4, label=f"{other}={val}")
    plt.xscale("log")
    plt.xlabel("janela j (simbolos)" if param == "j" else "degradacao p (%)")
    plt.ylabel("bits/byte")
    plt.title(f"A{bits}, modo {mode}, Kmax={k}: bits/byte vs {param}")
    plt.grid(alpha=.3, which="both")
    plt.legend(fontsize=8, ncol=2)
    out = os.path.join(FIGS, f"bpb_vs_{param}_A{bits}_{mode}.png")
    plt.tight_layout()
    plt.savefig(out, dpi=150)
    plt.close()
    print("wrote", out)


def plot_trace(path, bits, label, boundaries_bytes=None):
    """Save a progressive-rate figure from the trace CSV at `path`: bits
    emitted per symbol versus position. `boundaries_bytes` (optional) lists
    corpus-file offsets, drawn as labelled vertical lines."""
    ns, avgs = [], []
    with open(path, newline="") as f:
        for r in csv.DictReader(f):
            ns.append(int(r["n"]))
            avgs.append(float(r["avg"]))
    plt.figure(figsize=(8, 4))
    plt.plot(ns, avgs, lw=1)
    if boundaries_bytes:
        scale = 8.0 / bits
        for i, b in enumerate(boundaries_bytes):
            x = b * scale
            if x <= ns[-1]:
                plt.axvline(x, color="gray", lw=.6, ls="--")
                if i < len(boundaries_bytes) - 1:
                    mid = (x + boundaries_bytes[i + 1] * scale) / 2
                    plt.text(mid, plt.ylim()[1], str(i + 1), ha="center", va="top",
                             fontsize=7, color="gray")
    plt.xlabel("posicao n (simbolos)")
    plt.ylabel("bits acumulados / n")
    plt.title(label)
    plt.grid(alpha=.3)
    base = os.path.basename(path).replace(".csv", ".png")
    if not base.startswith("trace_"):
        base = "trace_" + base
    out = os.path.join(FIGS, base)
    plt.tight_layout()
    plt.savefig(out, dpi=150)
    plt.close()
    print("wrote", out)


def silesia_boundaries():
    """Cumulative byte offsets between the 12 Silesia files, in concat order."""
    d = os.path.join(ROOT, "data", "silesia")
    if not os.path.isdir(d):
        return []
    files = sorted(f for f in os.listdir(d) if os.path.isfile(os.path.join(d, f)))
    sizes = [os.path.getsize(os.path.join(d, f)) for f in files]
    out, total = [], 0
    for s in sizes[:-1]:
        total += s
        out.append(total)
    return out


def main():
    """Generate every figure whose input data exists and write
    results/summary_31.csv with the best configuration per alphabet/mode."""
    boundaries = silesia_boundaries()
    summary = []
    for bits in (1, 4, 8):
        path = os.path.join(RES, f"31_A{bits}.csv")
        if not os.path.exists(path):
            continue
        rows = load(path)
        plot_kmax(rows, bits)
        for mode in MODES:
            for param in ("j", "p"):
                plot_param(rows, bits, mode, param)
        for mode in MODES:
            k = best_kmax(rows, mode)
            sel = [r for r in rows if r["mode"] == mode]
            if not sel:
                continue
            best = min(sel, key=lambda r: r["bits_per_byte"])
            summary.append((bits, mode, best["kmax"], best["j"], best["p"],
                            best["bits_per_byte"], best["bits_per_symbol"]))
    if summary:
        out = os.path.join(RES, "summary_31.csv")
        with open(out, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["bits", "mode", "kmax", "j", "p", "bits_per_byte", "bits_per_symbol"])
            w.writerows(summary)
        print("wrote", out)
        print("\nbest per alphabet/mode:")
        for row in summary:
            print("  A%-2d %-6s kmax=%-2d j=%-6d p=%-2d  %.4f bits/byte" %
                  (row[0], row[1], row[2], row[3], row[4], row[5]))

    for bits in (1, 4, 8):
        path = os.path.join(RES, f"trace_dickens_A{bits}.csv")
        if os.path.exists(path):
            plot_trace(path, bits, f"Dickens, alfabeto A{bits}: comprimento medio progressivo")
    for bits in (1, 4, 8):
        for mode in MODES:
            path = os.path.join(RES, f"trace_silesia_A{bits}_{mode}.csv")
            if os.path.exists(path):
                plot_trace(path, bits, f"Silesia, A{bits}, modo {mode}", boundaries)


if __name__ == "__main__":
    main()
