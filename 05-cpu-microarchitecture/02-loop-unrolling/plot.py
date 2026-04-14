#!/usr/bin/env python3
from __future__ import annotations

import csv
from pathlib import Path
from typing import Dict, List

import matplotlib.pyplot as plt


ROOT_DIR = Path(__file__).resolve().parent
CSV_PATH = ROOT_DIR / "artifacts" / "data" / "loop_unrolling.csv"
PLOTS_DIR = ROOT_DIR / "plots"


def human_size(num_bytes: int) -> str:
    """
    Convert a byte size into a compact human-readable label.
    """
    if num_bytes >= 1024 * 1024:
        return f"{num_bytes // (1024 * 1024)} MiB"
    if num_bytes >= 1024:
        return f"{num_bytes // 1024} KiB"
    return f"{num_bytes} B"


def load_rows(csv_path: Path) -> List[dict]:
    """
    Load benchmark rows from CSV and cast numeric fields to useful types.
    """
    if not csv_path.exists():
        raise FileNotFoundError(f"CSV file not found: {csv_path}")

    rows: List[dict] = []
    with csv_path.open("r", newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            rows.append(
                {
                    "unroll": int(row["unroll"]),
                    "array_bytes": int(row["array_bytes"]),
                    "elements": int(row["elements"]),
                    "repeats": int(row["repeats"]),
                    "warmup": int(row["warmup"]),
                    "pin_cpu": int(row["pin_cpu"]),
                    "elapsed_ns": int(row["elapsed_ns"]),
                    "ns_per_element": float(row["ns_per_element"]),
                    "elements_per_sec": float(row["elements_per_sec"]),
                    "checksum": int(row["checksum"]),
                }
            )

    if not rows:
        raise ValueError(f"No benchmark rows found in: {csv_path}")

    return rows


def group_by_array_size(rows: List[dict]) -> Dict[int, List[dict]]:
    """
    Group rows by working-set size and sort each group by unroll factor.
    """
    groups: Dict[int, List[dict]] = {}
    for row in rows:
        groups.setdefault(row["array_bytes"], []).append(row)

    for array_bytes in groups:
        groups[array_bytes].sort(key=lambda r: r["unroll"])

    return dict(sorted(groups.items(), key=lambda item: item[0]))


def compute_speedup(groups: Dict[int, List[dict]]) -> Dict[int, List[dict]]:
    """
    Compute speedup relative to the unroll=1 baseline within each array size.
    Speedup is defined as:
        baseline_ns_per_element / current_ns_per_element
    """
    speedup_groups: Dict[int, List[dict]] = {}

    for array_bytes, rows in groups.items():
        baseline_candidates = [r for r in rows if r["unroll"] == 1]
        if not baseline_candidates:
            raise ValueError(f"Missing unroll=1 baseline for array_bytes={array_bytes}")

        baseline = baseline_candidates[0]["ns_per_element"]
        speedup_rows: List[dict] = []

        for row in rows:
            speedup_rows.append(
                {
                    "unroll": row["unroll"],
                    "speedup": baseline / row["ns_per_element"],
                }
            )

        speedup_groups[array_bytes] = speedup_rows

    return speedup_groups


def make_ns_per_element_plot(groups: Dict[int, List[dict]], out_path: Path) -> None:
    """
    Plot ns per element versus unroll factor for each working-set size.
    Lower is better.
    """
    plt.figure(figsize=(8, 5))

    for array_bytes, rows in groups.items():
        x = [r["unroll"] for r in rows]
        y = [r["ns_per_element"] for r in rows]
        plt.plot(x, y, marker="o", label=human_size(array_bytes))

    plt.title("Loop Unrolling: ns per Element vs Unroll Factor")
    plt.xlabel("Unroll Factor")
    plt.ylabel("ns per element")
    plt.xticks([1, 2, 4, 8, 16])
    plt.grid(True, alpha=0.3)
    plt.legend()
    plt.tight_layout()
    plt.savefig(out_path, dpi=160)
    plt.close()


def make_speedup_plot(speedup_groups: Dict[int, List[dict]], out_path: Path) -> None:
    """
    Plot speedup relative to unroll=1.
    Higher is better.
    """
    plt.figure(figsize=(8, 5))

    for array_bytes, rows in speedup_groups.items():
        x = [r["unroll"] for r in rows]
        y = [r["speedup"] for r in rows]
        plt.plot(x, y, marker="o", label=human_size(array_bytes))

    plt.title("Loop Unrolling: Speedup vs Unroll=1 Baseline")
    plt.xlabel("Unroll Factor")
    plt.ylabel("Speedup (x)")
    plt.xticks([1, 2, 4, 8, 16])
    plt.grid(True, alpha=0.3)
    plt.legend()
    plt.tight_layout()
    plt.savefig(out_path, dpi=160)
    plt.close()


def make_throughput_plot(groups: Dict[int, List[dict]], out_path: Path) -> None:
    """
    Plot throughput in elements per second versus unroll factor.
    Higher is better.
    """
    plt.figure(figsize=(8, 5))

    for array_bytes, rows in groups.items():
        x = [r["unroll"] for r in rows]
        y = [r["elements_per_sec"] for r in rows]
        plt.plot(x, y, marker="o", label=human_size(array_bytes))

    plt.title("Loop Unrolling: Throughput vs Unroll Factor")
    plt.xlabel("Unroll Factor")
    plt.ylabel("Elements per second")
    plt.xticks([1, 2, 4, 8, 16])
    plt.grid(True, alpha=0.3)
    plt.legend()
    plt.tight_layout()
    plt.savefig(out_path, dpi=160)
    plt.close()


def main() -> None:
    """
    Main entry point:
      1. Load CSV
      2. Group by working-set size
      3. Compute speedups
      4. Emit plots into plots/
    """
    PLOTS_DIR.mkdir(parents=True, exist_ok=True)

    rows = load_rows(CSV_PATH)
    groups = group_by_array_size(rows)
    speedup_groups = compute_speedup(groups)

    make_ns_per_element_plot(
        groups,
        PLOTS_DIR / "ns_per_element_vs_unroll.png",
    )
    make_speedup_plot(
        speedup_groups,
        PLOTS_DIR / "speedup_vs_unroll.png",
    )
    make_throughput_plot(
        groups,
        PLOTS_DIR / "elements_per_sec_vs_unroll.png",
    )

    print(f"[done] plots written to: {PLOTS_DIR}")


if __name__ == "__main__":
    main()
