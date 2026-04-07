#!/usr/bin/env python3
from pathlib import Path

import pandas as pd
import matplotlib.pyplot as plt


ROOT = Path(__file__).resolve().parent
CSV_PATH = ROOT / "artifacts" / "data" / "producer_consumer_queue.csv"
PLOTS_DIR = ROOT / "plots"


def ensure_plots_dir() -> None:
    PLOTS_DIR.mkdir(parents=True, exist_ok=True)


def load_data() -> pd.DataFrame:
    df = pd.read_csv(CSV_PATH)

    numeric_cols = [
        "producers",
        "consumers",
        "items_per_producer",
        "total_items",
        "capacity",
        "producer_work",
        "consumer_work",
        "pin_cpu",
        "elapsed_ns",
        "throughput_ops_per_sec",
        "ns_per_item",
        "produced",
        "consumed",
        "producer_full_wait_count",
        "producer_full_wait_ns",
        "consumer_empty_wait_count",
        "consumer_empty_wait_ns",
        "max_observed_queue_depth",
        "checksum",
    ]

    for col in numeric_cols:
        df[col] = pd.to_numeric(df[col], errors="coerce")

    return df


def save_plot(fig: plt.Figure, name: str) -> None:
    out_path = PLOTS_DIR / name
    fig.tight_layout()
    fig.savefig(out_path, dpi=160)
    plt.close(fig)
    print(f"[saved] {out_path}")


def plot_capacity_vs_throughput(df: pd.DataFrame) -> None:
    """
    Plot throughput against queue capacity for the fixed configuration:
      producers=2, consumers=2, producer_work=0
    grouped by consumer_work.
    """
    sub = df[
        (df["producers"] == 2)
        & (df["consumers"] == 2)
        & (df["producer_work"] == 0)
        & (df["items_per_producer"] == 500000)
    ].copy()

    if sub.empty:
        print("[skip] plot_capacity_vs_throughput: no matching rows")
        return

    fig, ax = plt.subplots(figsize=(8, 5))

    for consumer_work in sorted(sub["consumer_work"].unique()):
        s = sub[sub["consumer_work"] == consumer_work].sort_values("capacity")
        ax.plot(
            s["capacity"],
            s["throughput_ops_per_sec"],
            marker="o",
            label=f"consumer_work={consumer_work}",
        )

    ax.set_xscale("log", base=2)
    ax.set_xlabel("Queue capacity")
    ax.set_ylabel("Throughput (ops/sec)")
    ax.set_title("Throughput vs queue capacity")
    ax.grid(True, alpha=0.3)
    ax.legend()

    save_plot(fig, "capacity_vs_throughput.png")


def plot_capacity_vs_ns_per_item(df: pd.DataFrame) -> None:
    """
    Plot ns/item against queue capacity for the fixed configuration:
      producers=2, consumers=2, producer_work=0
    grouped by consumer_work.
    """
    sub = df[
        (df["producers"] == 2)
        & (df["consumers"] == 2)
        & (df["producer_work"] == 0)
        & (df["items_per_producer"] == 500000)
    ].copy()

    if sub.empty:
        print("[skip] plot_capacity_vs_ns_per_item: no matching rows")
        return

    fig, ax = plt.subplots(figsize=(8, 5))

    for consumer_work in sorted(sub["consumer_work"].unique()):
        s = sub[sub["consumer_work"] == consumer_work].sort_values("capacity")
        ax.plot(
            s["capacity"],
            s["ns_per_item"],
            marker="o",
            label=f"consumer_work={consumer_work}",
        )

    ax.set_xscale("log", base=2)
    ax.set_xlabel("Queue capacity")
    ax.set_ylabel("Nanoseconds per item")
    ax.set_title("Cost per item vs queue capacity")
    ax.grid(True, alpha=0.3)
    ax.legend()

    save_plot(fig, "capacity_vs_ns_per_item.png")


def plot_capacity_vs_producer_wait(df: pd.DataFrame) -> None:
    """
    Plot producer-side full-wait count against queue capacity for the fixed
    configuration:
      producers=2, consumers=2, producer_work=0
    grouped by consumer_work.
    """
    sub = df[
        (df["producers"] == 2)
        & (df["consumers"] == 2)
        & (df["producer_work"] == 0)
        & (df["items_per_producer"] == 500000)
    ].copy()

    if sub.empty:
        print("[skip] plot_capacity_vs_producer_wait: no matching rows")
        return

    fig, ax = plt.subplots(figsize=(8, 5))

    for consumer_work in sorted(sub["consumer_work"].unique()):
        s = sub[sub["consumer_work"] == consumer_work].sort_values("capacity")
        ax.plot(
            s["capacity"],
            s["producer_full_wait_count"],
            marker="o",
            label=f"consumer_work={consumer_work}",
        )

    ax.set_xscale("log", base=2)
    ax.set_xlabel("Queue capacity")
    ax.set_ylabel("Producer full-wait count")
    ax.set_title("Producer backpressure vs queue capacity")
    ax.grid(True, alpha=0.3)
    ax.legend()

    save_plot(fig, "capacity_vs_producer_wait_count.png")


def plot_capacity_vs_queue_depth(df: pd.DataFrame) -> None:
    """
    Plot observed max queue depth against queue capacity for the fixed
    configuration:
      producers=2, consumers=2, producer_work=0
    grouped by consumer_work.
    """
    sub = df[
        (df["producers"] == 2)
        & (df["consumers"] == 2)
        & (df["producer_work"] == 0)
        & (df["items_per_producer"] == 500000)
    ].copy()

    if sub.empty:
        print("[skip] plot_capacity_vs_queue_depth: no matching rows")
        return

    fig, ax = plt.subplots(figsize=(8, 5))

    for consumer_work in sorted(sub["consumer_work"].unique()):
        s = sub[sub["consumer_work"] == consumer_work].sort_values("capacity")
        ax.plot(
            s["capacity"],
            s["max_observed_queue_depth"],
            marker="o",
            label=f"consumer_work={consumer_work}",
        )

    ax.set_xscale("log", base=2)
    ax.set_xlabel("Queue capacity")
    ax.set_ylabel("Max observed queue depth")
    ax.set_title("Observed queue depth vs queue capacity")
    ax.grid(True, alpha=0.3)
    ax.legend()

    save_plot(fig, "capacity_vs_max_queue_depth.png")


def plot_imbalance_throughput(df: pd.DataFrame) -> None:
    """
    Plot throughput for the producer/consumer imbalance sweep:
      capacity=256, producer_work=0, consumer_work=100, items_per_producer=300000
    x-axis is a string label like '1P-4C'.
    """
    sub = df[
        (df["capacity"] == 256)
        & (df["producer_work"] == 0)
        & (df["consumer_work"] == 100)
        & (df["items_per_producer"] == 300000)
    ].copy()

    if sub.empty:
        print("[skip] plot_imbalance_throughput: no matching rows")
        return

    sub = sub.sort_values(["producers", "consumers"])
    sub["label"] = sub.apply(lambda r: f"{int(r['producers'])}P-{int(r['consumers'])}C", axis=1)

    fig, ax = plt.subplots(figsize=(9, 5))
    ax.bar(sub["label"], sub["throughput_ops_per_sec"])

    ax.set_xlabel("Producer / consumer configuration")
    ax.set_ylabel("Throughput (ops/sec)")
    ax.set_title("Throughput under producer-consumer imbalance")
    ax.grid(True, axis="y", alpha=0.3)

    save_plot(fig, "imbalance_throughput.png")


def plot_imbalance_waits(df: pd.DataFrame) -> None:
    """
    Plot both producer full-wait count and consumer empty-wait count
    across the producer/consumer imbalance sweep.
    """
    sub = df[
        (df["capacity"] == 256)
        & (df["producer_work"] == 0)
        & (df["consumer_work"] == 100)
        & (df["items_per_producer"] == 300000)
    ].copy()

    if sub.empty:
        print("[skip] plot_imbalance_waits: no matching rows")
        return

    sub = sub.sort_values(["producers", "consumers"])
    sub["label"] = sub.apply(lambda r: f"{int(r['producers'])}P-{int(r['consumers'])}C", axis=1)

    fig, ax = plt.subplots(figsize=(10, 5))
    ax.plot(sub["label"], sub["producer_full_wait_count"], marker="o", label="producer_full_wait_count")
    ax.plot(sub["label"], sub["consumer_empty_wait_count"], marker="o", label="consumer_empty_wait_count")

    ax.set_xlabel("Producer / consumer configuration")
    ax.set_ylabel("Wait count")
    ax.set_title("Blocking behavior under producer-consumer imbalance")
    ax.grid(True, alpha=0.3)
    ax.legend()

    save_plot(fig, "imbalance_wait_counts.png")


def print_text_summary(df: pd.DataFrame) -> None:
    print("[summary] rows =", len(df))

    cap_sub = df[
        (df["producers"] == 2)
        & (df["consumers"] == 2)
        & (df["producer_work"] == 0)
        & (df["items_per_producer"] == 500000)
    ].copy()

    if not cap_sub.empty:
        print("\n[capacity sweep summary]")
        print(
            cap_sub[
                [
                    "capacity",
                    "consumer_work",
                    "throughput_ops_per_sec",
                    "ns_per_item",
                    "producer_full_wait_count",
                    "consumer_empty_wait_count",
                    "max_observed_queue_depth",
                ]
            ]
            .sort_values(["consumer_work", "capacity"])
            .to_string(index=False)
        )

    imb_sub = df[
        (df["capacity"] == 256)
        & (df["producer_work"] == 0)
        & (df["consumer_work"] == 100)
        & (df["items_per_producer"] == 300000)
    ].copy()

    if not imb_sub.empty:
        print("\n[imbalance sweep summary]")
        print(
            imb_sub[
                [
                    "producers",
                    "consumers",
                    "throughput_ops_per_sec",
                    "ns_per_item",
                    "producer_full_wait_count",
                    "consumer_empty_wait_count",
                ]
            ]
            .sort_values(["producers", "consumers"])
            .to_string(index=False)
        )


def main() -> None:
    ensure_plots_dir()
    df = load_data()

    print_text_summary(df)

    plot_capacity_vs_throughput(df)
    plot_capacity_vs_ns_per_item(df)
    plot_capacity_vs_producer_wait(df)
    plot_capacity_vs_queue_depth(df)
    plot_imbalance_throughput(df)
    plot_imbalance_waits(df)

    print(f"[done] plots directory: {PLOTS_DIR}")


if __name__ == "__main__":
    main()
