#!/usr/bin/env python3
"""
Plot stride-vs-cache-miss benchmark results.

Input:
  artifacts/data/stride_vs_cache_miss.csv

Output:
  ../plots/stride_vs_cache_miss_ns_per_access_by_ws.png
  ../plots/stride_vs_cache_miss_ws_panels.png
  ../plots/stride_vs_cache_miss_summary.csv

Notes:
- The script assumes it is run from the lab directory:
    02-memory-hierarchy/04-stride-vs-cache-miss
- Plots are written outside artifacts, into ./plots relative to the lab root.
"""

from pathlib import Path
import pandas as pd
import matplotlib.pyplot as plt


def main() -> None:
    lab_dir = Path(__file__).resolve().parent
    data_path = lab_dir / "artifacts" / "data" / "stride_vs_cache_miss.csv"
    plots_dir = lab_dir / "plots"
    plots_dir.mkdir(parents=True, exist_ok=True)

    df = pd.read_csv(data_path)

    # Basic sanity checks
    required = {
        "working_set_bytes",
        "working_set_kb",
        "stride_elems",
        "stride_bytes",
        "total_accesses",
        "repeats",
        "best_elapsed_ns",
        "ns_per_access",
    }
    missing = required - set(df.columns)
    if missing:
        raise ValueError(f"Missing required columns: {sorted(missing)}")

    # Sort for stable plotting
    df = df.sort_values(["working_set_bytes", "stride_bytes"]).reset_index(drop=True)

    # Human-readable working set label
    def ws_label(num_bytes: int) -> str:
        kb = num_bytes / 1024
        mb = kb / 1024
        if mb >= 1:
            return f"{mb:.0f} MB"
        return f"{kb:.0f} KB"

    df["working_set_label"] = df["working_set_bytes"].map(ws_label)

    # Save a compact summary table
    summary = df[
        [
            "working_set_bytes",
            "working_set_kb",
            "working_set_label",
            "stride_elems",
            "stride_bytes",
            "ns_per_access",
        ]
    ].copy()
    summary.to_csv(plots_dir / "stride_vs_cache_miss_summary.csv", index=False)

    # Plot 1: all working sets on one log-x plot
    plt.figure(figsize=(10, 6))
    for ws_bytes, group in df.groupby("working_set_bytes", sort=True):
        plt.plot(
            group["stride_bytes"],
            group["ns_per_access"],
            marker="o",
            label=ws_label(int(ws_bytes)),
        )

    plt.xscale("log", base=2)
    plt.xlabel("Stride (bytes, log2 scale)")
    plt.ylabel("Best ns per access")
    plt.title("Stride vs. access cost across working set sizes")
    plt.grid(True, alpha=0.3)
    plt.legend()
    plt.tight_layout()
    plt.savefig(plots_dir / "stride_vs_cache_miss_ns_per_access_by_ws.png", dpi=200)
    plt.close()

    # Plot 2: one panel per working set
    unique_ws = sorted(df["working_set_bytes"].unique())
    fig, axes = plt.subplots(len(unique_ws), 1, figsize=(9, 3.2 * len(unique_ws)), sharex=True)

    if len(unique_ws) == 1:
        axes = [axes]

    for ax, ws_bytes in zip(axes, unique_ws):
        group = df[df["working_set_bytes"] == ws_bytes]
        ax.plot(group["stride_bytes"], group["ns_per_access"], marker="o")
        ax.set_xscale("log", base=2)
        ax.set_ylabel("ns/access")
        ax.set_title(f"Working set = {ws_label(int(ws_bytes))}")
        ax.grid(True, alpha=0.3)

    axes[-1].set_xlabel("Stride (bytes, log2 scale)")
    fig.suptitle("Stride vs. access cost by working set", y=0.995)
    fig.tight_layout()
    fig.savefig(plots_dir / "stride_vs_cache_miss_ws_panels.png", dpi=200)
    plt.close(fig)

    print("[done]")
    print(f"data   : {data_path}")
    print(f"plots  : {plots_dir / 'stride_vs_cache_miss_ns_per_access_by_ws.png'}")
    print(f"plots  : {plots_dir / 'stride_vs_cache_miss_ws_panels.png'}")
    print(f"summary: {plots_dir / 'stride_vs_cache_miss_summary.csv'}")


if __name__ == "__main__":
    main()
