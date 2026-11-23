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
    """Load the results CSV at `path` and cast its numeric columns. Returns a
    list of row dicts, or an empty list when the file does not exist."""
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
    """Format `x` with three decimals and a comma decimal separator (LaTeX)."""
    return f"{x:.3f}".replace(".", ",")


def f1(x):
    """Format `x` with one decimal, thousands separator and comma decimal."""
    s = f"{x:,.1f}"
    return s.replace(",", "X").replace(".", ",").replace("X", ".")


def best(rows, mode, kmax=None):
    """Return the row with the smallest bits/byte for `mode`, optionally
    restricted to one `kmax`; returns None when no row matches."""
    sel = [r for r in rows if r["mode"] == mode and (kmax is None or r["kmax"] == kmax)]
    return min(sel, key=lambda r: r["bits_per_byte"]) if sel else None


def write(path, text):
    """Write `text` to `path`, creating its parent directory, and print a note."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(text)
    print("wrote", path)


def kmax_table(rows, bits):
    """Write tables/31_A{bits}_kmax.tex: bits/byte and bits/symbol per Kmax,
    using the best (j,p) of each mode in `rows`."""
    kmaxes = sorted({r["kmax"] for r in rows})
    lines = [
        r"\begin{table}[H]",
        r"\centering",
        r"\small",
        rf"\caption{{Bits/byte e bits/s\'imbolo por $K_{{\max}}$, alfabeto A{bits}, "
        rf"melhor $(j,p)$ de cada modo.}}",
        rf"\label{{tab:kmax-A{bits}}}",
        r"\begin{tabular}{rrrrrrr}",
        r"\toprule",
        r"$K_{\max}$ & \multicolumn{2}{c}{none} & \multicolumn{2}{c}{reset} & \multicolumn{2}{c}{prune} \\",
        r"\cmidrule(lr){2-3}\cmidrule(lr){4-5}\cmidrule(lr){6-7}",
        r" & b/byte & b/s\'im. & b/byte & b/s\'im. & b/byte & b/s\'im. \\",
        r"\midrule",
    ]
    for k in kmaxes:
        cells = []
        for mode in MODES:
            b = best(rows, mode, k)
            cells.extend([f3(b["bits_per_byte"]), f3(b["bits_per_symbol"])] if b else ["-", "-"])
        lines.append(f"{k} & " + " & ".join(cells) + r" \\")
    lines += [r"\bottomrule", r"\end{tabular}", r"\end{table}", ""]
    write(os.path.join(OUT, f"31_A{bits}_kmax.tex"), "\n".join(lines))


def summary_table(allrows):
    """Write tables/31_summary.tex: best configuration per alphabet and mode,
    with the gain over the best `none` row. `allrows` maps bits -> rows."""
    lines = [
        r"\begin{table}[H]",
        r"\centering",
        r"\setlength{\tabcolsep}{5pt}",
        r"\caption{Melhor configuração por alfabeto e modo no fluxo de calibração "
        r"(ganho relativo ao melhor \emph{none} da calibração).}",
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
            gain = "-"
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
    """Write tables/commercial.tex from the reference-compressor CSV at
    `path`; does nothing if the file is missing or has no rows."""
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


def dec_table(path):
    """Write tables/dec_times.tex from the isolated encode/decode timing CSV at
    `path`; does nothing when the input is missing."""
    rows = read(path)
    if not rows:
        return
    order = {"none": 0, "reset": 1, "prune": 2}
    rows.sort(key=lambda r: (r["bits"], order.get(r["mode"], 9)))
    lines = [
        r"\begin{table}[H]",
        r"\centering",
        r"\setlength{\tabcolsep}{5pt}",
        r"\caption{Tempos de compress\~ao e descompress\~ao das melhores "
        r"configura\c{c}\~oes da calibra\c{c}\~ao, em execu\c{c}\~ao isolada "
        r"(uma thread, 12 MB, \texttt{mem\_cap}=8 milh\~oes; no modo \emph{none}, "
        r"$j$ e $p$ n\~ao afetam).}",
        r"\label{tab:dec-times}",
        r"\begin{tabular}{llrrrrrr}",
        r"\toprule",
        r"A & modo & $K_{\max}$ & $j$ & $p$ & bits/byte & comp. (s) & desc. (s) \\",
        r"\midrule",
    ]
    for r in rows:
        lines.append(
            f"A{r['bits']} & {r['mode']} & {r['kmax']} & {r['j']} & {r['p']} & "
            f"{f3(r['bits_per_byte'])} & {f1(r['enc_s'])} & {f1(r['dec_s'])} \\\\"
        )
    lines += [r"\bottomrule", r"\end{tabular}", r"\end{table}", ""]
    write(os.path.join(OUT, "dec_times.tex"), "\n".join(lines))



def times_kmax_table(rows, bits):
    """Write tables/times_A{bits}_kmax.tex: encode/decode times per Kmax for
    each mode, taking the rows of alphabet `bits` from `rows`."""
    if not rows:
        return
    by = {(r["kmax"], r["mode"]): r for r in rows if r["bits"] == bits}
    kmaxes = sorted({r["kmax"] for r in rows if r["bits"] == bits})
    lines = [
        r"\begin{table}[H]",
        r"\centering",
        r"\small",
        rf"\caption{{Tempos de compress\~ao e descompress\~ao por $K_{{\max}}$, "
        rf"alfabeto A{bits}, na melhor $(j,p)$ de cada modo "
        rf"(execu\c{{c}}\~ao isolada, 12 MB, \texttt{{mem\_cap}}=8 milh\~oes).}}",
        rf"\label{{tab:times-A{bits}}}",
        r"\begin{tabular}{rrrrrrr}",
        r"\toprule",
        r"$K_{\max}$ & \multicolumn{2}{c}{none} & \multicolumn{2}{c}{reset} & \multicolumn{2}{c}{prune} \\",
        r"\cmidrule(lr){2-3}\cmidrule(lr){4-5}\cmidrule(lr){6-7}",
        r" & comp. & desc. & comp. & desc. & comp. & desc. \\",
        r"\midrule",
    ]
    for k in kmaxes:
        cells = []
        for mode in ("none", "reset", "prune"):
            r = by.get((k, mode))
            cells += [f1(r["enc_s"]), f1(r["dec_s"])] if r else ["-", "-"]
        lines.append(f"{k} & " + " & ".join(cells) + r" \\")
    lines += [r"\bottomrule", r"\end{tabular}", r"\end{table}", ""]
    write(os.path.join(OUT, f"times_A{bits}_kmax.tex"), "\n".join(lines))


def final_table(results_dir):
    """Write tables/final.tex from every final_A{bits}_{mode}.csv found in
    `results_dir`; does nothing when no input rows exist."""
    rows = []
    for bits in (1, 4, 8):
        for mode in MODES:
            rows += read(os.path.join(results_dir, f"final_A{bits}_{mode}.csv"))
    if not rows:
        return
    lines = [
        r"\begin{table}[H]",
        r"\centering",
        r"\footnotesize",
        r"\setlength{\tabcolsep}{3pt}",
        r"\caption{Configura\c{c}\~oes finais no corpus Silesia completo (uma thread, "
        r"\texttt{mem\_cap}=20 milh\~oes; mesma configura\c{c}\~ao $(K_{\max},j,p)$ "
        r"nos tr\^es modos de cada alfabeto; no modo \emph{none}, $j$ e $p$ n\~ao afetam).}",
        r"\label{tab:final}",
        r"\resizebox{\linewidth}{!}{%",
        r"\begin{tabular}{llrrrrrrrr}",
        r"\toprule",
        r"A & modo & $K_{\max}$ & $j$ & $p$ & bits/byte & bits/s\'im. & comp. (s) & desc. (s) & reset/poda \\",
        r"\midrule",
    ]
    for r in rows:
        lines.append(
            f"A{r['bits']} & {r['mode']} & {r['kmax']} & {r['j']} & {r['p']} & "
            f"{f3(r['bits_per_byte'])} & {f3(r['bits_per_symbol'])} & "
            f"{f1(r['enc_s'])} & {f1(r['dec_s'])} & "
            f"{r['resets']}/{r['prunes']} \\\\"
        )
    lines += [r"\bottomrule", r"\end{tabular}", r"}", r"\end{table}", ""]
    write(os.path.join(OUT, "final.tex"), "\n".join(lines))


def main():
    """Parse --results/--out and regenerate every table whose input data
    exists (writes into the output directory)."""
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
    dec_table(os.path.join(args.results, "31_dec_times.csv"))
    arows = read(os.path.join(args.results, "31_times_by_kmax.csv"))
    for bits in (1, 4, 8):
        times_kmax_table(arows, bits)
    final_table(args.results)


if __name__ == "__main__":
    main()
