#!/usr/bin/env python3
"""Builds Markdown tables for section 3.1 from results/31_A*.csv.

    python3 scripts/summarize.py > results/tables_31.md
"""
import csv
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RES = os.path.join(ROOT, "results")


def load(bits):
    """Read and cast the 3.1 grid rows for alphabet `bits`; returns an empty
    list when the CSV is missing."""
    path = os.path.join(RES, f"31_A{bits}.csv")
    if not os.path.exists(path):
        return []
    rows = []
    with open(path, newline="") as f:
        for r in csv.DictReader(f):
            if not r["payload_bytes"]:
                continue
            r["kmax"] = int(r["kmax"])
            r["j"] = int(r["j"])
            r["p"] = int(r["p"])
            r["bits_per_byte"] = float(r["bits_per_byte"])
            r["bits_per_symbol"] = float(r["bits_per_symbol"])
            r["enc_s"] = float(r["enc_s"])
            r["resets"] = int(r["resets"])
            r["prunes"] = int(r["prunes"])
            rows.append(r)
    return rows


def best(rows, mode, kmax=None):
    """Return the row with the smallest bits/byte for `mode`, optionally
    restricted to one `kmax`; returns None when no row matches."""
    sel = [r for r in rows if r["mode"] == mode and (kmax is None or r["kmax"] == kmax)]
    if not sel:
        return None
    return min(sel, key=lambda r: r["bits_per_byte"])


def main():
    """Print the Markdown tables for section 3.1 (per alphabet, Kmax effects,
    best reset parameters and the commercial reference) to stdout."""
    print("# Resultados da secao 3.1\n")
    print("Fluxo de calibracao: 1 MB de cada um dos 12 arquivos Silesia "
          "(12 MB, 11 transicoes). `bits/byte` inclui apenas o payload "
          "aritmetico, sem o cabecalho de 32 bytes.\n")
    for bits in (1, 4, 8):
        rows = load(bits)
        if not rows:
            continue
        print(f"\n## Alfabeto A{bits}\n")
        base = best(rows, "none")
        print("| modo | Kmax | j | p | bits/byte | bits/simbolo | tempo (s) | resets | podas |")
        print("|---|---|---|---|---|---|---|---|---|")
        for mode in ("none", "reset", "prune"):
            b = best(rows, mode)
            if not b:
                continue
            print(f"| {mode} | {b['kmax']} | {b['j']} | {b['p']} | "
                  f"{b['bits_per_byte']:.4f} | {b['bits_per_symbol']:.4f} | "
                  f"{b['enc_s']:.1f} | {b['resets']} | {b['prunes']} |")
        if base:
            print()
            for mode in ("reset", "prune"):
                b = best(rows, mode)
                if b:
                    gain = 100.0 * (base["bits_per_byte"] - b["bits_per_byte"]) / base["bits_per_byte"]
                    print(f"- {mode} vs. sem adaptacao: **{gain:+.2f}%**")
        print("\n### Efeito de Kmax (melhor j,p por modo)\n")
        print("| Kmax | none | reset | prune |")
        print("|---|---|---|---|")
        kmaxes = sorted({r["kmax"] for r in rows})
        for k in kmaxes:
            cells = []
            for mode in ("none", "reset", "prune"):
                b = best(rows, mode, k)
                cells.append(f"{b['bits_per_byte']:.3f}" if b else "-")
            print(f"| {k} | " + " | ".join(cells) + " |")
        b = best(rows, "reset")
        if b:
            print(f"\n### Melhores (j,p) para reset com Kmax={b['kmax']}\n")
            sel = sorted((r for r in rows if r["mode"] == "reset" and r["kmax"] == b["kmax"]),
                         key=lambda r: r["bits_per_byte"])[:10]
            print("| j | p | bits/byte | resets |")
            print("|---|---|---|---|")
            for r in sel:
                print(f"| {r['j']} | {r['p']} | {r['bits_per_byte']:.4f} | {r['resets']} |")

    comp = os.path.join(RES, "commercial.csv")
    if os.path.exists(comp):
        print("\n## Referencia comercial (arquivo inteiro)\n")
        print("| ferramenta | arquivo | metodo | bytes | bits/byte |")
        print("|---|---|---|---|---|")
        with open(comp, newline="") as f:
            for r in csv.DictReader(f):
                print(f"| {r['tool']} | {os.path.basename(r['file'])} | {r['method']} | "
                      f"{r['out_bytes']} | {r['bits_per_byte']} |")


if __name__ == "__main__":
    main()
