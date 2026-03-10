#!/usr/bin/env python3

"""
plot.py

Generate plots for the context-switch lab.

Input
-----
artifacts/data/context_switch.csv

Output
------
plots/context_switch_roundtrip.png
plots/context_switch_estimated_ctxswitch.png
plots/context_switch_roundtrip_logscale.png
"""

import csv
import math
import os
from collections import defaultdict

import matplotlib.pyplot as plt


DATA_PATH = "artifacts/data/context_switch.csv"
PLOTS_DIR = "plots"

ROUNDTRIP_PLOT = os.path.join(PLOTS_DIR, "context_switch_roundtrip.png")
CTXSWITCH_PLOT = os.path.join(PLOTS_DIR, "context_switch_estimated_ctxswitch.png")
ROUNDTRIP_LOG_PLOT = os.path.join(PLOTS_DIR, "context_switch_roundtrip_logscale.png")


def mean(values):
    if not values:
        raise ValueError("mean() received an empty list")
    return sum(values) / len(values)


def stddev(values):
    if not values:
        raise ValueError("stddev() received an empty list")
    if len(values) <= 1:
        return 0.0
    m = mean(values)
    variance = sum((x - m) ** 2 for x in values) / (len(values) - 1)
    return math.sqrt(variance)


def load_data():
    """
    Load CSV rows and group them by mode.

    Expected columns:
        mode,iterations,warmup,parent_cpu,child_cpu,elapsed_ns,
        ns_per_roundtrip,ns_per_context_switch_est
    """
    data = defaultdict(lambda: {"roundtrip": [], "ctxswitch": []})

    with open(DATA_PATH, newline="") as f:
        reader = csv.DictReader(f)

        required = {
            "mode",
            "ns_per_roundtrip",
            "ns_per_context_switch_est",
        }
        missing = required - set(reader.fieldnames or [])
        if missing:
            raise ValueError(f"Missing required CSV columns: {sorted(missing)}")

        for row in reader:
            mode = row["mode"].strip()
            data[mode]["roundtrip"].append(float(row["ns_per_roundtrip"]))
            data[mode]["ctxswitch"].append(float(row["ns_per_context_switch_est"]))

    return data


def summarize(data):
    """
    Compute mean and sample standard deviation for each mode.
    """
    summary = {}

    for mode, metrics in data.items():
        summary[mode] = {
            "roundtrip_mean": mean(metrics["roundtrip"]),
            "roundtrip_std": stddev(metrics["roundtrip"]),
            "ctxswitch_mean": mean(metrics["ctxswitch"]),
            "ctxswitch_std": stddev(metrics["ctxswitch"]),
            "n": len(metrics["roundtrip"]),
        }

    return summary


def bar_plot(labels, means, stds, ylabel, title, path, log_scale=False):
    """
    Draw a bar plot with error bars.

    Parameters
    ----------
    labels : list[str]
        Category labels such as ["same", "split"].
    means : list[float]
        Mean value for each category.
    stds : list[float]
        Standard deviation for each category.
    ylabel : str
        Label for the y-axis.
    title : str
        Figure title.
    path : str
        Output image path.
    log_scale : bool
        If True, use a logarithmic y-axis.
    """
    plt.figure(figsize=(7, 5))

    bars = plt.bar(labels, means, yerr=stds, capsize=6)

    plt.ylabel(ylabel)
    plt.title(title)

    if log_scale:
        plt.yscale("log")

    # Add value labels above the bars.
    # For log scale, place the text slightly above the bar multiplicatively.
    for bar, val in zip(bars, means):
        x = bar.get_x() + bar.get_width() / 2
        y = bar.get_height()

        if log_scale:
            text_y = y * 1.08
        else:
            text_y = y + max(stds) * 0.05 if stds else y

        plt.text(x, text_y, f"{val:.1f}", ha="center", va="bottom")

    plt.tight_layout()
    plt.savefig(path, dpi=200)
    plt.close()


def main():
    if not os.path.exists(DATA_PATH):
        raise FileNotFoundError(
            f"{DATA_PATH} not found. Run ./run.sh first."
        )

    os.makedirs(PLOTS_DIR, exist_ok=True)

    data = load_data()
    summary = summarize(data)

    # Keep the plot order stable.
    order = ["same", "split"]
    labels = [m for m in order if m in summary]

    roundtrip_means = [summary[m]["roundtrip_mean"] for m in labels]
    roundtrip_stds = [summary[m]["roundtrip_std"] for m in labels]

    ctx_means = [summary[m]["ctxswitch_mean"] for m in labels]
    ctx_stds = [summary[m]["ctxswitch_std"] for m in labels]

    print("\nContext Switch Summary\n")
    for m in labels:
        s = summary[m]
        print(
            f"{m:>5} | n={s['n']} | "
            f"roundtrip {s['roundtrip_mean']:.2f} ns ± {s['roundtrip_std']:.2f} | "
            f"ctx-switch {s['ctxswitch_mean']:.2f} ns ± {s['ctxswitch_std']:.2f}"
        )

    # Linear-scale roundtrip plot
    bar_plot(
        labels=labels,
        means=roundtrip_means,
        stds=roundtrip_stds,
        ylabel="Latency (ns)",
        title="Ping-Pong Roundtrip Latency",
        path=ROUNDTRIP_PLOT,
        log_scale=False,
    )

    # Linear-scale estimated context-switch plot
    bar_plot(
        labels=labels,
        means=ctx_means,
        stds=ctx_stds,
        ylabel="Estimated latency per context switch (ns)",
        title="Estimated Context Switch Cost",
        path=CTXSWITCH_PLOT,
        log_scale=False,
    )

    # Log-scale roundtrip plot for large gaps between same and split
    bar_plot(
        labels=labels,
        means=roundtrip_means,
        stds=roundtrip_stds,
        ylabel="Latency (ns, log scale)",
        title="Ping-Pong Roundtrip Latency (Log Scale)",
        path=ROUNDTRIP_LOG_PLOT,
        log_scale=True,
    )

    print("\nSaved plots:")
    print(f"  {ROUNDTRIP_PLOT}")
    print(f"  {CTXSWITCH_PLOT}")
    print(f"  {ROUNDTRIP_LOG_PLOT}")


if __name__ == "__main__":
    main()
