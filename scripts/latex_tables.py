#!/usr/bin/env python3
"""Generate LaTeX tables for the report from the result CSVs.

    python3 scripts/latex_tables.py [--results DIR] [--out DIR]

Writes report/tables/*.tex.  Missing inputs are skipped, so it can be run
while the sweep is still going.
"""
import argparse
import csv
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MODES = ("none", "reset", "prune")


def read(path):
    if not os.path.exists(path):
        return []
    out = []
    with open(path, newline="") as f:
        for r in csv.DictReader(f):
            if not r.get("payload_bytes"):
                continue
            for k in ("bits", "kmax", "j", "p"):
                r[k] = int(r[k])
            for k in ("bits_per_symbol", "bits_per_byte", "enc_s", "dec_s"):
                r[k] = float(r[k] or 0)
            r["resets"] = int(r["resets"] or 0)
            r["prunes"] = int(r["prunes"] or 0)
            out.append(r)
    return out


def f3(x):
    return f"{x:.3f}".replace(".", ",")


def best(rows, mode, kmax=None):
    sel = [r for r in rows if r["mode"] == mode and (kmax is None or r["kmax"] == kmax)]
    return min(sel, key=lambda r: r["bits_per_byte"]) if sel else None


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(text)
    print("wrote", path)


def kmax_table(rows, bits):
    kmaxes = sorted({r["kmax"] for r in rows})
    lines = [
        r"\begin{table}[H]",
        r"\centering",
        rf"\caption{{Bits/byte por $K_{{\max}}$, alfabeto A{bits}, "
        rf"melhor $(j,p)$ de cada modo.}}",
        rf"\label{{tab:kmax-A{bits}}}",
        r"\begin{tabular}{rrrr}",
        r"\toprule",
        r"$K_{\max}$ & none & reset & prune \\",
        r"\midrule",
    ]
    for k in kmaxes:
        cells = []
        for mode in MODES:
            b = best(rows, mode, k)
            cells.append(f3(b["bits_per_byte"]) if b else "--")
        lines.append(f"{k} & " + " & ".join(cells) + r" \\")
    lines += [r"\bottomrule", r"\end{tabular}", r"\end{table}", ""]
    write(os.path.join(OUT, f"31_A{bits}_kmax.tex"), "\n".join(lines))


def summary_table(allrows):
    lines = [
        r"\begin{table}[H]",
        r"\centering",
        r"\caption{Melhor configuração por alfabeto e modo no fluxo de calibração.}",
        r"\label{tab:resumo-31}",
        r"\begin{tabular}{llrrrrrr}",
        r"\toprule",
        r"Alfabeto & Modo & $K_{\max}$ & $j$ & $p$ & bits/byte & bits/sím. & ganho \\",
        r"\midrule",
    ]
    for bits in (1, 4, 8):
        rows = allrows.get(bits, [])
        if not rows:
            continue
        base = best(rows, "none")
        for mode in MODES:
            b = best(rows, mode)
            if not b:
                continue
            gain = "--"
            if base and mode != "none":
                g = 100.0 * (base["bits_per_byte"] - b["bits_per_byte"]) / base["bits_per_byte"]
                gain = f"{g:+.2f}".replace(".", ",") + r"\,\%"
            lines.append(
                f"A{bits} & {mode} & {b['kmax']} & {b['j']} & {b['p']} & "
                f"{f3(b['bits_per_byte'])} & {f3(b['bits_per_symbol'])} & {gain} \\\\"
            )
        lines.append(r"\midrule")
    lines[-1] = r"\bottomrule"
    lines += [r"\end{tabular}", r"\end{table}", ""]
    write(os.path.join(OUT, "31_summary.tex"), "\n".join(lines))


def commercial_table(path):
    if not os.path.exists(path):
        return
    rows = []
    with open(path, newline="") as f:
        for r in csv.DictReader(f):
            if not r.get("out_bytes"):
                continue
            rows.append(r)
    if not rows:
        return
    lines = [
        r"\begin{table}[H]",
        r"\centering",
        r"\caption{Comparação com compressores de referência (7-Zip, uma thread).}",
        r"\label{tab:comercial}",
        r"\begin{tabular}{llrr}",
        r"\toprule",
        r"Arquivo & Método & bytes & bits/byte \\",
        r"\midrule",
    ]
    for r in rows:
        lines.append(
            f"\\texttt{{{os.path.basename(r['file'])}}} & \\texttt{{{r['method']}}} & "
            f"{int(r['out_bytes'])} & {f3(float(r['bits_per_byte']))} \\\\"
        )
    lines += [r"\bottomrule", r"\end{tabular}", r"\end{table}", ""]
    write(os.path.join(OUT, "commercial.tex"), "\n".join(lines))


def final_table(results_dir):
    rows = []
    for bits in (1, 4, 8):
        for mode in MODES:
            rows += read(os.path.join(results_dir, f"final_A{bits}_{mode}.csv"))
    if not rows:
        return
    lines = [
        r"\begin{table}[H]",
        r"\centering",
        r"\caption{Configurações finais no corpus Silesia completo (uma thread).}",
        r"\label{tab:final}",
        r"\begin{tabular}{llrrrrrrr}",
        r"\toprule",
        r"A & modo & $K_{\max}$ & $j$ & $p$ & bits/byte & comp. (s) & desc. (s) & adv. \\",
        r"\midrule",
    ]
    for r in rows:
        lines.append(
            f"A{r['bits']} & {r['mode']} & {r['kmax']} & {r['j']} & {r['p']} & "
            f"{f3(r['bits_per_byte'])} & {r['enc_s']:.1f} & {r['dec_s']:.1f} & "
            f"{r['resets']}/{r['prunes']} \\\\"
        )
    lines += [r"\bottomrule", r"\end{tabular}", r"\end{table}", ""]
    write(os.path.join(OUT, "final.tex"), "\n".join(lines))


def main():
    global OUT
    ap = argparse.ArgumentParser()
    ap.add_argument("--results", default=os.path.join(ROOT, "results"))
    ap.add_argument("--out", default=os.path.join(ROOT, "report", "tables"))
    args = ap.parse_args()
    OUT = args.out
    os.makedirs(OUT, exist_ok=True)

    allrows = {}
    for bits in (1, 4, 8):
        rows = read(os.path.join(args.results, f"31_A{bits}.csv"))
        if rows:
            allrows[bits] = rows
            kmax_table(rows, bits)
    if allrows:
        summary_table(allrows)
    commercial_table(os.path.join(args.results, "commercial.csv"))
    final_table(args.results)


if __name__ == "__main__":
    main()
