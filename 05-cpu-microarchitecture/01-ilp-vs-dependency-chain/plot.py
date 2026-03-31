#!/usr/bin/env python3
"""
plot.py

Generate plots for the ILP vs dependency-chain benchmark.

Expected input CSV:
    artifacts/data/ilp_vs_dependency_chain.csv

Output directory:
    plots/

This script is intentionally robust:
- It accepts CSV files with or without a header row.
- It tolerates extra blank lines.
- It validates required columns before plotting.

Generated plots:
1. Best ns/op by mode
2. Average ns/op by mode with min/max range shown as error bars
3. All runs ns/op by mode (scatter/line style)

Usage:
    python3 plot.py

Optional:
    python3 plot.py --input artifacts/data/ilp_vs_dependency_chain.csv --output-dir plots
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path

import matplotlib.pyplot as plt
import pandas as pd


EXPECTED_COLUMNS = [
    "mode",
    "iters",
    "warmup",
    "pin_cpu",
    "elapsed_ns",
    "ops",
    "ns_per_op",
    "checksum",
]

MODE_ORDER = ["chain1", "indep2", "indep4", "indep8"]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Plot ILP benchmark results.")
    parser.add_argument(
        "--input",
        default="artifacts/data/ilp_vs_dependency_chain.csv",
        help="Path to the benchmark CSV file.",
    )
    parser.add_argument(
        "--output-dir",
        default="plots",
        help="Directory where plots will be saved.",
    )
    return parser.parse_args()


def load_csv(csv_path: Path) -> pd.DataFrame:
    """
    Load the CSV file.

    The benchmark output may or may not contain a header row, so this function
    handles both cases.
    """
    if not csv_path.exists():
        raise FileNotFoundError(f"Input CSV not found: {csv_path}")

    # First attempt: load normally and see whether expected columns are present.
    try:
        df = pd.read_csv(csv_path)
        if all(col in df.columns for col in EXPECTED_COLUMNS):
            return df
    except Exception:
        pass

    # Fallback: load as headerless CSV.
    df = pd.read_csv(csv_path, header=None, names=EXPECTED_COLUMNS)

    # Drop rows that do not look like real data.
    # This helps in case the file contains blank rows or malformed content.
    df = df[df["mode"].notna()].copy()
    df = df[df["mode"].astype(str).isin(MODE_ORDER)].copy()

    return df


def clean_dataframe(df: pd.DataFrame) -> pd.DataFrame:
    """
    Validate and normalize the DataFrame.
    """
    missing = [col for col in EXPECTED_COLUMNS if col not in df.columns]
    if missing:
        raise ValueError(f"Missing required columns: {missing}")

    # Keep only rows for the known benchmark modes.
    df = df[df["mode"].astype(str).isin(MODE_ORDER)].copy()

    # Convert numeric columns to proper numeric types.
    numeric_cols = [
        "iters",
        "warmup",
        "pin_cpu",
        "elapsed_ns",
        "ops",
        "ns_per_op",
        "checksum",
    ]
    for col in numeric_cols:
        df[col] = pd.to_numeric(df[col], errors="coerce")

    # Drop broken rows after conversion.
    df = df.dropna(subset=["mode", "elapsed_ns", "ops", "ns_per_op"]).copy()

    # Make mode an ordered categorical column for stable plotting order.
    df["mode"] = pd.Categorical(df["mode"], categories=MODE_ORDER, ordered=True)
    df = df.sort_values(["mode", "ns_per_op"]).reset_index(drop=True)

    if df.empty:
        raise ValueError("No valid benchmark rows found in the CSV.")

    return df


def summarize(df: pd.DataFrame) -> pd.DataFrame:
    """
    Compute per-mode summary statistics.
    """
    summary = (
        df.groupby("mode", observed=True)["ns_per_op"]
        .agg(["mean", "min", "max", "std", "count"])
        .reset_index()
        .rename(
            columns={
                "mean": "avg_ns_per_op",
                "min": "best_ns_per_op",
                "max": "worst_ns_per_op",
                "std": "std_ns_per_op",
                "count": "runs",
            }
        )
    )

    # Error bar lengths for avg plot.
    summary["err_low"] = summary["avg_ns_per_op"] - summary["best_ns_per_op"]
    summary["err_high"] = summary["worst_ns_per_op"] - summary["avg_ns_per_op"]

    return summary


def save_best_plot(summary: pd.DataFrame, out_dir: Path) -> None:
    """
    Save a bar chart of best ns/op by mode.
    Lower is better.
    """
    plt.figure(figsize=(8, 5))
    plt.bar(summary["mode"].astype(str), summary["best_ns_per_op"])
    plt.xlabel("Mode")
    plt.ylabel("Best ns/op")
    plt.title("ILP vs Dependency Chain - Best ns/op by Mode")
    plt.tight_layout()
    plt.savefig(out_dir / "ilp_best_ns_per_op.png", dpi=200)
    plt.close()


def save_avg_plot(summary: pd.DataFrame, out_dir: Path) -> None:
    """
    Save an average ns/op plot with min/max range shown as error bars.
    """
    plt.figure(figsize=(8, 5))
    plt.bar(
        summary["mode"].astype(str),
        summary["avg_ns_per_op"],
        yerr=[summary["err_low"], summary["err_high"]],
        capsize=5,
    )
    plt.xlabel("Mode")
    plt.ylabel("Average ns/op")
    plt.title("ILP vs Dependency Chain - Average ns/op by Mode")
    plt.tight_layout()
    plt.savefig(out_dir / "ilp_avg_ns_per_op.png", dpi=200)
    plt.close()


def save_all_runs_plot(df: pd.DataFrame, out_dir: Path) -> None:
    """
    Save a plot that shows every measured run for each mode.

    We map modes to x positions and draw a small line+marker series per mode,
    so the user can see both spread and monotonic improvement.
    """
    plt.figure(figsize=(8, 5))

    x_positions = {mode: idx for idx, mode in enumerate(MODE_ORDER)}

    for mode in MODE_ORDER:
        mode_df = df[df["mode"].astype(str) == mode].copy()
        if mode_df.empty:
            continue

        # Give each repeated run a tiny x offset so multiple points are visible.
        xs = [x_positions[mode] + (i - (len(mode_df) - 1) / 2) * 0.03 for i in range(len(mode_df))]
        ys = mode_df["ns_per_op"].tolist()

        plt.plot(xs, ys, marker="o")

    plt.xticks(range(len(MODE_ORDER)), MODE_ORDER)
    plt.xlabel("Mode")
    plt.ylabel("ns/op")
    plt.title("ILP vs Dependency Chain - All Runs")
    plt.tight_layout()
    plt.savefig(out_dir / "ilp_all_runs_ns_per_op.png", dpi=200)
    plt.close()


def print_text_summary(df: pd.DataFrame, summary: pd.DataFrame) -> None:
    """
    Print a short textual summary to stdout.
    """
    print("[summary] runs by mode")
    print(summary.to_string(index=False))
    print()

    if "chain1" in summary["mode"].astype(str).values:
        base = float(summary.loc[summary["mode"].astype(str) == "chain1", "best_ns_per_op"].iloc[0])
        print("[summary] speedup vs chain1 (best ns/op basis)")
        for _, row in summary.iterrows():
            mode = str(row["mode"])
            best = float(row["best_ns_per_op"])
            speedup = base / best
            print(f"  {mode:>6s}: best_ns_per_op={best:.6f}, speedup_vs_chain1={speedup:.3f}x")
        print()

    print(f"[summary] total rows loaded: {len(df)}")


def main() -> None:
    args = parse_args()

    input_path = Path(args.input)
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    df = load_csv(input_path)
    df = clean_dataframe(df)
    summary = summarize(df)

    print_text_summary(df, summary)

    save_best_plot(summary, output_dir)
    save_avg_plot(summary, output_dir)
    save_all_runs_plot(df, output_dir)

    print(f"[done] wrote plots to: {output_dir.resolve()}")
    print(f"  - {output_dir / 'ilp_best_ns_per_op.png'}")
    print(f"  - {output_dir / 'ilp_avg_ns_per_op.png'}")
    print(f"  - {output_dir / 'ilp_all_runs_ns_per_op.png'}")


if __name__ == "__main__":
    main()
