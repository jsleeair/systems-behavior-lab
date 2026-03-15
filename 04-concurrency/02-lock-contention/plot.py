#!/usr/bin/env python3
from pathlib import Path

import pandas as pd
import matplotlib.pyplot as plt


def ensure_plots_dir() -> Path:
    """
    Create the output directory if it does not already exist.

    The user requested that plots be written outside artifacts/,
    so we use ./plots relative to the lab root.
    """
    out_dir = Path("plots")
    out_dir.mkdir(parents=True, exist_ok=True)
    return out_dir


def load_data(csv_path: str) -> pd.DataFrame:
    """
    Load experiment results from CSV and sort them for stable plotting.
    """
    df = pd.read_csv(csv_path)

    numeric_cols = [
        "threads",
        "iters_per_thread",
        "cs_work",
        "outside_work",
        "pin_cpu",
        "elapsed_ns",
        "total_ops",
        "ns_per_op",
        "mops_per_sec",
        "final_counter",
        "sink",
    ]

    for col in numeric_cols:
        df[col] = pd.to_numeric(df[col])

    df = df.sort_values(["threads", "cs_work", "outside_work"]).reset_index(drop=True)
    return df


def save_throughput_vs_threads_baseline(df: pd.DataFrame, out_dir: Path) -> None:
    """
    Plot throughput vs thread count for the baseline case:
    cs_work=0, outside_work=0.

    This is the cleanest view of pure lock contention on a tiny critical section.
    """
    subset = df[(df["cs_work"] == 0) & (df["outside_work"] == 0)].copy()
    subset = subset.sort_values("threads")

    plt.figure(figsize=(7, 4.5))
    plt.plot(subset["threads"], subset["mops_per_sec"], marker="o")
    plt.xlabel("Threads")
    plt.ylabel("Throughput (Mops/s)")
    plt.title("Lock contention baseline: throughput vs threads\n(cs_work=0, outside_work=0)")
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    plt.savefig(out_dir / "throughput_vs_threads_cs0_out0.png", dpi=200)
    plt.close()


def save_ns_per_op_vs_threads_baseline(df: pd.DataFrame, out_dir: Path) -> None:
    """
    Plot latency cost per operation vs thread count for the baseline case.
    """
    subset = df[(df["cs_work"] == 0) & (df["outside_work"] == 0)].copy()
    subset = subset.sort_values("threads")

    plt.figure(figsize=(7, 4.5))
    plt.plot(subset["threads"], subset["ns_per_op"], marker="o")
    plt.xlabel("Threads")
    plt.ylabel("ns/op")
    plt.title("Lock contention baseline: ns/op vs threads\n(cs_work=0, outside_work=0)")
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    plt.savefig(out_dir / "ns_per_op_vs_threads_cs0_out0.png", dpi=200)
    plt.close()


def save_throughput_vs_cs_work(df: pd.DataFrame, out_dir: Path, threads: int = 8, outside_work: int = 0) -> None:
    """
    Show how increasing critical-section work reduces throughput for a fixed
    thread count and outside-work level.
    """
    subset = df[(df["threads"] == threads) & (df["outside_work"] == outside_work)].copy()
    subset = subset.sort_values("cs_work")

    plt.figure(figsize=(7, 4.5))
    plt.plot(subset["cs_work"], subset["mops_per_sec"], marker="o")
    plt.xlabel("Critical-section work (cs_work)")
    plt.ylabel("Throughput (Mops/s)")
    plt.title(f"Throughput vs critical-section work\n(threads={threads}, outside_work={outside_work})")
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    plt.savefig(out_dir / f"throughput_vs_cs_work_threads{threads}_out{outside_work}.png", dpi=200)
    plt.close()


def save_throughput_vs_outside_work(df: pd.DataFrame, out_dir: Path, threads: int = 8, cs_work: int = 0) -> None:
    """
    Show how changing outside-work affects overall throughput for a fixed
    thread count and critical-section size.

    Note: this is overall throughput, so if outside_work itself becomes large,
    total throughput can still fall because total CPU work per operation rises.
    """
    subset = df[(df["threads"] == threads) & (df["cs_work"] == cs_work)].copy()
    subset = subset.sort_values("outside_work")

    plt.figure(figsize=(7, 4.5))
    plt.plot(subset["outside_work"], subset["mops_per_sec"], marker="o")
    plt.xlabel("Outside-lock work (outside_work)")
    plt.ylabel("Throughput (Mops/s)")
    plt.title(f"Throughput vs outside work\n(threads={threads}, cs_work={cs_work})")
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    plt.savefig(out_dir / f"throughput_vs_outside_work_threads{threads}_cs{cs_work}.png", dpi=200)
    plt.close()


def save_heatmap_throughput_out0(df: pd.DataFrame, out_dir: Path) -> None:
    """
    Save a simple heatmap-like image for throughput with outside_work=0.

    Rows    : cs_work
    Columns : threads

    This makes it easy to see how scalability degrades as both thread count
    and critical-section length increase.
    """
    subset = df[df["outside_work"] == 0].copy()

    pivot = subset.pivot_table(
        index="cs_work",
        columns="threads",
        values="mops_per_sec",
        aggfunc="mean",
    )

    plt.figure(figsize=(7, 4.8))
    plt.imshow(pivot.values, aspect="auto", interpolation="nearest")
    plt.xticks(range(len(pivot.columns)), pivot.columns)
    plt.yticks(range(len(pivot.index)), pivot.index)
    plt.xlabel("Threads")
    plt.ylabel("cs_work")
    plt.title("Throughput heatmap (Mops/s)\n(outside_work=0)")
    plt.colorbar(label="Mops/s")
    plt.tight_layout()
    plt.savefig(out_dir / "heatmap_throughput_out0.png", dpi=200)
    plt.close()


def print_text_summary(df: pd.DataFrame) -> None:
    """
    Print a small textual summary to make quick terminal inspection easier.
    """
    print("[summary] baseline (cs_work=0, outside_work=0)")
    baseline = df[(df["cs_work"] == 0) & (df["outside_work"] == 0)].sort_values("threads")
    for _, row in baseline.iterrows():
        print(
            f"  threads={int(row['threads'])} "
            f"throughput={row['mops_per_sec']:.3f} Mops/s "
            f"ns/op={row['ns_per_op']:.2f}"
        )

    print()
    print("[summary] threads=8, outside_work=0")
    cs_slice = df[(df["threads"] == 8) & (df["outside_work"] == 0)].sort_values("cs_work")
    for _, row in cs_slice.iterrows():
        print(
            f"  cs_work={int(row['cs_work'])} "
            f"throughput={row['mops_per_sec']:.3f} Mops/s "
            f"ns/op={row['ns_per_op']:.2f}"
        )


def main() -> None:
    csv_path = "artifacts/data/lock_contention.csv"
    out_dir = ensure_plots_dir()
    df = load_data(csv_path)

    print_text_summary(df)

    save_throughput_vs_threads_baseline(df, out_dir)
    save_ns_per_op_vs_threads_baseline(df, out_dir)
    save_throughput_vs_cs_work(df, out_dir, threads=8, outside_work=0)
    save_throughput_vs_outside_work(df, out_dir, threads=8, cs_work=0)
    save_heatmap_throughput_out0(df, out_dir)

    print()
    print(f"[done] plots written to: {out_dir.resolve()}")


if __name__ == "__main__":
    main()
