#!/usr/bin/env python3
from pathlib import Path
import pandas as pd
import matplotlib.pyplot as plt


def main() -> None:
    root = Path(__file__).resolve().parent
    csv_path = root / "artifacts" / "data" / "memory_ordering.csv"
    plots_dir = root / "plots"
    plots_dir.mkdir(exist_ok=True)

    if not csv_path.exists():
        raise FileNotFoundError(f"CSV not found: {csv_path}")

    df = pd.read_csv(csv_path)

    required = {
        "mode",
        "iterations",
        "elapsed_ns",
        "ns_per_trial",
        "count_00",
        "count_01",
        "count_10",
        "count_11",
    }
    missing = required - set(df.columns)
    if missing:
        raise ValueError(f"Missing columns in CSV: {sorted(missing)}")

    mode_order = ["relaxed", "acqrel", "seqcst"]
    df["mode"] = pd.Categorical(df["mode"], categories=mode_order, ordered=True)
    df = df.sort_values("mode").reset_index(drop=True)

    # ------------------------------------------------------------
    # Plot 1: both-zero count by mode
    # ------------------------------------------------------------
    fig, ax = plt.subplots(figsize=(8, 5))

    grouped_00 = [df.loc[df["mode"] == mode, "count_00"].tolist() for mode in mode_order]
    positions = range(len(mode_order))

    ax.boxplot(grouped_00, positions=list(positions), widths=0.5)

    for i, mode in enumerate(mode_order):
        ys = df.loc[df["mode"] == mode, "count_00"].tolist()
        xs = [i + 1] * len(ys)
        ax.scatter(xs, ys)

    means_00 = df.groupby("mode", observed=True)["count_00"].mean().reindex(mode_order)
    for i, mode in enumerate(mode_order, start=1):
        ax.scatter([i], [means_00[mode]], marker="x", s=80)

    ax.set_title("Both-zero outcome count (r0=0, r1=0) by memory order")
    ax.set_xlabel("Mode")
    ax.set_ylabel("count_00 per run")
    ax.set_xticks([1, 2, 3])
    ax.set_xticklabels(mode_order)
    ax.grid(True, axis="y", alpha=0.3)

    out1 = plots_dir / "both_zero_by_mode.png"
    fig.tight_layout()
    fig.savefig(out1, dpi=200)
    plt.close(fig)

    # ------------------------------------------------------------
    # Plot 2: average outcome ratio by mode
    # ------------------------------------------------------------
    ratio_df = df.copy()
    ratio_df["ratio_00"] = ratio_df["count_00"] / ratio_df["iterations"]
    ratio_df["ratio_01"] = ratio_df["count_01"] / ratio_df["iterations"]
    ratio_df["ratio_10"] = ratio_df["count_10"] / ratio_df["iterations"]
    ratio_df["ratio_11"] = ratio_df["count_11"] / ratio_df["iterations"]

    ratio_mean = (
        ratio_df.groupby("mode", observed=True)[["ratio_00", "ratio_01", "ratio_10", "ratio_11"]]
        .mean()
        .reindex(mode_order)
    )

    fig, ax = plt.subplots(figsize=(9, 5))

    x = list(range(len(mode_order)))
    width = 0.18

    ax.bar([v - 1.5 * width for v in x], ratio_mean["ratio_00"], width=width, label="00")
    ax.bar([v - 0.5 * width for v in x], ratio_mean["ratio_01"], width=width, label="01")
    ax.bar([v + 0.5 * width for v in x], ratio_mean["ratio_10"], width=width, label="10")
    ax.bar([v + 1.5 * width for v in x], ratio_mean["ratio_11"], width=width, label="11")

    ax.set_title("Average outcome ratio by memory order")
    ax.set_xlabel("Mode")
    ax.set_ylabel("Average fraction of trials")
    ax.set_xticks(x)
    ax.set_xticklabels(mode_order)
    ax.legend(title="Outcome")
    ax.grid(True, axis="y", alpha=0.3)

    out2 = plots_dir / "outcome_ratio_by_mode.png"
    fig.tight_layout()
    fig.savefig(out2, dpi=200)
    plt.close(fig)

    # ------------------------------------------------------------
    # Plot 3: ns_per_trial by mode
    # ------------------------------------------------------------
    fig, ax = plt.subplots(figsize=(8, 5))

    grouped_ns = [df.loc[df["mode"] == mode, "ns_per_trial"].tolist() for mode in mode_order]
    ax.boxplot(grouped_ns, positions=[1, 2, 3], widths=0.5)

    for i, mode in enumerate(mode_order, start=1):
        ys = df.loc[df["mode"] == mode, "ns_per_trial"].tolist()
        xs = [i] * len(ys)
        ax.scatter(xs, ys)

    means_ns = df.groupby("mode", observed=True)["ns_per_trial"].mean().reindex(mode_order)
    for i, mode in enumerate(mode_order, start=1):
        ax.scatter([i], [means_ns[mode]], marker="x", s=80)

    ax.set_title("Runtime per trial by memory order")
    ax.set_xlabel("Mode")
    ax.set_ylabel("ns_per_trial")
    ax.set_xticks([1, 2, 3])
    ax.set_xticklabels(mode_order)
    ax.grid(True, axis="y", alpha=0.3)

    out3 = plots_dir / "ns_per_trial_by_mode.png"
    fig.tight_layout()
    fig.savefig(out3, dpi=200)
    plt.close(fig)

    # ------------------------------------------------------------
    # Text summary
    # ------------------------------------------------------------
    print("[summary] average both-zero count by mode")
    print(
        df.groupby("mode", observed=True)["count_00"]
        .mean()
        .reindex(mode_order)
        .to_string()
    )
    print()

    print("[summary] average outcome ratios by mode")
    print(ratio_mean.to_string())
    print()

    print("[summary] average ns_per_trial by mode")
    print(
        df.groupby("mode", observed=True)["ns_per_trial"]
        .mean()
        .reindex(mode_order)
        .to_string()
    )
    print()

    print("[done] wrote:")
    print(f"  {out1}")
    print(f"  {out2}")
    print(f"  {out3}")


if __name__ == "__main__":
    main()
