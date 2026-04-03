#!/usr/bin/env python3
"""
plot.py

Read page fault breakdown benchmark results and generate plots under ../plots.

Expected input:
    artifacts/data/page_fault_cost_breakdown.csv

Outputs:
    ../plots/page_fault_cost_breakdown_ns_per_page.png
    ../plots/page_fault_cost_breakdown_minflt.png
    ../plots/page_fault_cost_breakdown_majflt.png
    ../plots/page_fault_cost_breakdown_faults_per_page.png

This script:
- creates ../plots if it does not exist
- prints grouped summary statistics
- generates bar charts with mean +/- std

Usage:
    python3 plot.py
"""

from pathlib import Path
import sys

import matplotlib.pyplot as plt
import pandas as pd


def require_columns(df: pd.DataFrame, required: list[str]) -> None:
    missing = [c for c in required if c not in df.columns]
    if missing:
        raise ValueError(f"Missing required columns: {missing}")


def load_data(csv_path: Path) -> pd.DataFrame:
    if not csv_path.exists():
        raise FileNotFoundError(f"CSV file not found: {csv_path}")

    df = pd.read_csv(csv_path)

    require_columns(
        df,
        [
            "mode",
            "pages",
            "page_size",
            "total_bytes",
            "repeats",
            "warmup",
            "pin_cpu",
            "run_index",
            "elapsed_ns",
            "minflt_delta",
            "majflt_delta",
            "ns_per_page",
            "checksum",
        ],
    )

    numeric_cols = [
        "pages",
        "page_size",
        "total_bytes",
        "repeats",
        "warmup",
        "pin_cpu",
        "run_index",
        "elapsed_ns",
        "minflt_delta",
        "majflt_delta",
        "ns_per_page",
        "checksum",
    ]
    for col in numeric_cols:
        df[col] = pd.to_numeric(df[col], errors="raise")

    # Derived metrics are useful for comparing modes that fault at different rates.
    df["minflt_per_page"] = df["minflt_delta"] / df["pages"]
    df["majflt_per_page"] = df["majflt_delta"] / df["pages"]
    df["total_faults"] = df["minflt_delta"] + df["majflt_delta"]
    df["total_faults_per_page"] = df["total_faults"] / df["pages"]

    return df


def summarize_metric(df: pd.DataFrame, metric: str) -> pd.DataFrame:
    grouped = (
        df.groupby("mode", sort=False)[metric]
        .agg(["mean", "std", "min", "max", "count"])
        .reset_index()
    )
    return grouped


def print_environment_summary(df: pd.DataFrame) -> None:
    first = df.iloc[0]
    print("[environment]")
    print(f"  pages       = {int(first['pages'])}")
    print(f"  page_size   = {int(first['page_size'])}")
    print(f"  total_bytes = {int(first['total_bytes'])}")
    print(f"  repeats     = {int(first['repeats'])}")
    print(f"  warmup      = {int(first['warmup'])}")
    print(f"  pin_cpu     = {int(first['pin_cpu'])}")
    print()


def print_metric_summary(df: pd.DataFrame, metric: str) -> None:
    summary = summarize_metric(df, metric)

    print(f"[summary] {metric}")
    for _, row in summary.iterrows():
        std_value = row["std"]
        if pd.isna(std_value):
            std_value = 0.0
        print(
            f"  {row['mode']:<28} "
            f"mean={row['mean']:>10.2f} "
            f"std={std_value:>10.2f} "
            f"min={row['min']:>10.2f} "
            f"max={row['max']:>10.2f} "
            f"n={int(row['count'])}"
        )
    print()


def plot_bar_with_error(
    summary: pd.DataFrame,
    metric: str,
    ylabel: str,
    title: str,
    output_path: Path,
) -> None:
    x = range(len(summary))
    means = summary["mean"]
    errors = summary["std"].fillna(0.0)

    plt.figure(figsize=(12, 6))
    plt.bar(x, means, yerr=errors, capsize=5)
    plt.xticks(list(x), summary["mode"], rotation=25, ha="right")
    plt.ylabel(ylabel)
    plt.title(title)
    plt.tight_layout()
    plt.savefig(output_path, dpi=200)
    plt.close()


def main() -> None:
    script_dir = Path(__file__).resolve().parent
    csv_path = script_dir / "artifacts" / "data" / "page_fault_cost_breakdown.csv"
    plots_dir = script_dir.parent / "plots"
    plots_dir.mkdir(parents=True, exist_ok=True)

    df = load_data(csv_path)

    print_environment_summary(df)

    metrics = [
        "ns_per_page",
        "minflt_delta",
        "majflt_delta",
        "minflt_per_page",
        "majflt_per_page",
        "total_faults_per_page",
    ]
    for metric in metrics:
        print_metric_summary(df, metric)

    # 1) Main performance plot
    summary_ns = summarize_metric(df, "ns_per_page")
    plot_bar_with_error(
        summary=summary_ns,
        metric="ns_per_page",
        ylabel="Nanoseconds per page",
        title="Page Fault Cost Breakdown: ns per page",
        output_path=plots_dir / "page_fault_cost_breakdown_ns_per_page.png",
    )

    # 2) Minor faults per run
    summary_minflt = summarize_metric(df, "minflt_delta")
    plot_bar_with_error(
        summary=summary_minflt,
        metric="minflt_delta",
        ylabel="Minor faults per run",
        title="Page Fault Cost Breakdown: minor faults per run",
        output_path=plots_dir / "page_fault_cost_breakdown_minflt.png",
    )

    # 3) Major faults per run
    summary_majflt = summarize_metric(df, "majflt_delta")
    plot_bar_with_error(
        summary=summary_majflt,
        metric="majflt_delta",
        ylabel="Major faults per run",
        title="Page Fault Cost Breakdown: major faults per run",
        output_path=plots_dir / "page_fault_cost_breakdown_majflt.png",
    )

    # 4) Faults per page (normalized)
    summary_faults_per_page = summarize_metric(df, "total_faults_per_page")
    plot_bar_with_error(
        summary=summary_faults_per_page,
        metric="total_faults_per_page",
        ylabel="Faults per page",
        title="Page Fault Cost Breakdown: total faults per page",
        output_path=plots_dir / "page_fault_cost_breakdown_faults_per_page.png",
    )

    print("[done] generated plots:")
    for path in [
        plots_dir / "page_fault_cost_breakdown_ns_per_page.png",
        plots_dir / "page_fault_cost_breakdown_minflt.png",
        plots_dir / "page_fault_cost_breakdown_majflt.png",
        plots_dir / "page_fault_cost_breakdown_faults_per_page.png",
    ]:
        print(f"  {path}")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"[error] {exc}", file=sys.stderr)
        sys.exit(1)
