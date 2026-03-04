#!/usr/bin/env python3
"""
Plot comparison for TLB effect lab results.

Inputs (default):
  - artifacts/out/tlb_4k.csv
  - artifacts/out/tlb_2m.csv

Outputs:
  - Shows interactive plots (if GUI is available)
  - Also saves PNG files into artifacts/out/

How to run:
  python3 plot_compare.py
  python3 plot_compare.py --outdir artifacts/out
"""

import argparse
import os
import sys

import pandas as pd
import matplotlib.pyplot as plt


def load_csv(path: str) -> pd.DataFrame:
    if not os.path.exists(path):
        raise FileNotFoundError(f"Missing file: {path}")
    df = pd.read_csv(path)

    required = {"pages", "bytes", "cycles_per_access", "ns_per_access"}
    missing = required - set(df.columns)
    if missing:
        raise ValueError(f"{path} is missing columns: {sorted(missing)}")

    # Ensure numeric
    for c in ["pages", "bytes", "cycles_per_access", "ns_per_access"]:
        df[c] = pd.to_numeric(df[c], errors="coerce")

    df = df.dropna(subset=["pages", "bytes", "cycles_per_access", "ns_per_access"])
    df = df.sort_values("pages")
    return df


def ensure_outdir(path: str) -> None:
    os.makedirs(path, exist_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser(description="Compare TLB lab CSV outputs.")
    parser.add_argument("--csv4k", default="artifacts/out/tlb_4k.csv",
                        help="Path to 4KB baseline CSV")
    parser.add_argument("--csv2m", default="artifacts/out/tlb_2m.csv",
                        help="Path to THP/2M-hint CSV")
    parser.add_argument("--outdir", default="artifacts/out",
                        help="Output directory for PNG files")
    parser.add_argument("--no-show", action="store_true",
                        help="Do not open interactive windows (still saves PNGs)")
    args = parser.parse_args()

    try:
        df4k = load_csv(args.csv4k)
        df2m = load_csv(args.csv2m)
    except Exception as e:
        print(f"[error] {e}", file=sys.stderr)
        return 1

    ensure_outdir(args.outdir)

    # ---- Plot 1: cycles per access vs pages ----
    plt.figure()
    plt.plot(df4k["pages"], df4k["cycles_per_access"], marker="o", label="4KB baseline")
    plt.plot(df2m["pages"], df2m["cycles_per_access"], marker="o", label="THP hint")
    plt.xscale("log")
    plt.xlabel("Pages touched (log scale)")
    plt.ylabel("Cycles per access")
    plt.title("TLB Effect: Cycles per access vs Pages")
    plt.legend()
    plt.grid(True)
    out1 = os.path.join(args.outdir, "compare_cycles_vs_pages.png")
    plt.savefig(out1, dpi=160, bbox_inches="tight")

    # ---- Plot 2: ns per access vs pages ----
    plt.figure()
    plt.plot(df4k["pages"], df4k["ns_per_access"], marker="o", label="4KB baseline")
    plt.plot(df2m["pages"], df2m["ns_per_access"], marker="o", label="THP hint")
    plt.xscale("log")
    plt.xlabel("Pages touched (log scale)")
    plt.ylabel("Nanoseconds per access")
    plt.title("TLB Effect: ns per access vs Pages")
    plt.legend()
    plt.grid(True)
    out2 = os.path.join(args.outdir, "compare_ns_vs_pages.png")
    plt.savefig(out2, dpi=160, bbox_inches="tight")

    # ---- Plot 3: cycles per access vs working set bytes ----
    plt.figure()
    plt.plot(df4k["bytes"], df4k["cycles_per_access"], marker="o", label="4KB baseline")
    plt.plot(df2m["bytes"], df2m["cycles_per_access"], marker="o", label="THP hint")
    plt.xscale("log")
    plt.xlabel("Working set size (bytes, log scale)")
    plt.ylabel("Cycles per access")
    plt.title("TLB Effect: Cycles per access vs Working set size")
    plt.legend()
    plt.grid(True)
    out3 = os.path.join(args.outdir, "compare_cycles_vs_bytes.png")
    plt.savefig(out3, dpi=160, bbox_inches="tight")

    print("[ok] Saved plots:")
    print(f"  {out1}")
    print(f"  {out2}")
    print(f"  {out3}")

    if not args.no_show:
        plt.show()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
