#!/usr/bin/env python3
"""
plot.py

Generate plots for the SIMD vs scalar microbenchmark.

Input:
  artifacts/data/simd_vs_scalar.csv

Output directory:
  plots/

Generated figures:
  1) ns_per_element, misalign=0
  2) ns_per_element, misalign=4
  3) speedup vs scalar_novec, misalign=0
  4) speedup vs scalar_novec, misalign=4

Why these plots:
  - The first two show absolute performance across working-set sizes.
  - The last two show relative gains over the true scalar baseline.

Usage:
  python3 plot.py
"""

from pathlib import Path
import csv
import math
import matplotlib.pyplot as plt


ROOT_DIR = Path(__file__).resolve().parent
CSV_PATH = ROOT_DIR / "artifacts" / "data" / "simd_vs_scalar.csv"
PLOTS_DIR = ROOT_DIR / "plots"

MODES = ["scalar_novec", "scalar_auto", "avx2"]


def load_rows(csv_path: Path):
    """
    Load benchmark rows from CSV and convert numeric fields to useful types.
    """
    rows = []

    with csv_path.open("r", newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            rows.append(
                {
                    "mode": row["mode"],
                    "elements": int(row["elements"]),
                    "repeats": int(row["repeats"]),
                    "warmup": int(row["warmup"]),
                    "pin_cpu": int(row["pin_cpu"]),
                    "misalign_bytes": int(row["misalign_bytes"]),
                    "elapsed_ns": int(row["elapsed_ns"]),
                    "ns_per_element": float(row["ns_per_element"]),
                    "checksum": float(row["checksum"]),
                }
            )

    return rows


def filter_rows(rows, misalign_bytes: int):
    """
    Return only rows for a specific misalignment value.
    """
    return [row for row in rows if row["misalign_bytes"] == misalign_bytes]


def build_series(rows):
    """
    Build per-mode series sorted by array length.

    Returns:
      {
        "scalar_novec": [(elements, ns_per_element), ...],
        "scalar_auto":  [(elements, ns_per_element), ...],
        "avx2":         [(elements, ns_per_element), ...],
      }
    """
    series = {mode: [] for mode in MODES}

    for row in rows:
        mode = row["mode"]
        if mode not in series:
            continue
        series[mode].append((row["elements"], row["ns_per_element"]))

    for mode in MODES:
        series[mode].sort(key=lambda x: x[0])

    return series


def build_speedup_series(rows):
    """
    Build speedup series relative to scalar_novec for the same element count.

    speedup = baseline_ns_per_element / mode_ns_per_element

    Returns:
      {
        "scalar_auto": [(elements, speedup), ...],
        "avx2":        [(elements, speedup), ...],
      }
    """
    baseline = {}
    for row in rows:
        if row["mode"] == "scalar_novec":
            baseline[row["elements"]] = row["ns_per_element"]

    series = {
        "scalar_auto": [],
        "avx2": [],
    }

    for row in rows:
        mode = row["mode"]
        if mode not in series:
            continue

        elements = row["elements"]
        if elements not in baseline:
            continue

        base_ns = baseline[elements]
        cur_ns = row["ns_per_element"]

        if cur_ns <= 0.0:
            continue

        speedup = base_ns / cur_ns
        series[mode].append((elements, speedup))

    for mode in series:
        series[mode].sort(key=lambda x: x[0])

    return series


def plot_ns_per_element(series, misalign_bytes: int, output_path: Path):
    """
    Plot absolute performance in ns_per_element for each mode.
    """
    plt.figure(figsize=(9, 6))

    for mode in MODES:
        points = series.get(mode, [])
        if not points:
            continue

        xs = [x for x, _ in points]
        ys = [y for _, y in points]

        plt.plot(xs, ys, marker="o", label=mode)

    plt.xscale("log", base=2)
    plt.xlabel("Elements")
    plt.ylabel("ns per element")
    plt.title(f"SIMD vs Scalar: ns_per_element (misalign_bytes={misalign_bytes})")
    plt.grid(True, alpha=0.3)
    plt.legend()
    plt.tight_layout()
    plt.savefig(output_path, dpi=150)
    plt.close()


def plot_speedup(series, misalign_bytes: int, output_path: Path):
    """
    Plot speedup relative to scalar_novec.
    """
    plt.figure(figsize=(9, 6))

    for mode in ["scalar_auto", "avx2"]:
        points = series.get(mode, [])
        if not points:
            continue

        xs = [x for x, _ in points]
        ys = [y for _, y in points]

        plt.plot(xs, ys, marker="o", label=mode)

    # Reference line: 1.0x means equal to scalar_novec
    xs_all = []
    for points in series.values():
        xs_all.extend([x for x, _ in points])

    if xs_all:
        plt.plot(
            [min(xs_all), max(xs_all)],
            [1.0, 1.0],
            linestyle="--",
            label="scalar_novec baseline",
        )

    plt.xscale("log", base=2)
    plt.xlabel("Elements")
    plt.ylabel("Speedup over scalar_novec")
    plt.title(f"SIMD vs Scalar: speedup vs scalar_novec (misalign_bytes={misalign_bytes})")
    plt.grid(True, alpha=0.3)
    plt.legend()
    plt.tight_layout()
    plt.savefig(output_path, dpi=150)
    plt.close()


def print_console_summary(rows):
    """
    Print a short textual summary so the script is useful even before opening plots.
    """
    print("[summary] loaded rows:", len(rows))

    for misalign in [0, 4]:
        subset = filter_rows(rows, misalign)
        series = build_series(subset)

        print(f"[summary] misalign_bytes={misalign}")
        for mode in MODES:
            points = series.get(mode, [])
            if not points:
                continue

            first_x, first_y = points[0]
            last_x, last_y = points[-1]
            print(
                f"  mode={mode:13s} "
                f"smallest={first_x:8d} -> {first_y:.6f} ns/elem, "
                f"largest={last_x:8d} -> {last_y:.6f} ns/elem"
            )


def main():
    if not CSV_PATH.exists():
        raise FileNotFoundError(f"CSV not found: {CSV_PATH}")

    PLOTS_DIR.mkdir(parents=True, exist_ok=True)

    rows = load_rows(CSV_PATH)
    print_console_summary(rows)

    for misalign in [0, 4]:
        subset = filter_rows(rows, misalign)
        ns_series = build_series(subset)
        speedup_series = build_speedup_series(subset)

        ns_out = PLOTS_DIR / f"simd_vs_scalar_ns_per_element_misalign_{misalign}.png"
        speedup_out = PLOTS_DIR / f"simd_vs_scalar_speedup_misalign_{misalign}.png"

        plot_ns_per_element(ns_series, misalign, ns_out)
        plot_speedup(speedup_series, misalign, speedup_out)

        print(f"[saved] {ns_out}")
        print(f"[saved] {speedup_out}")

    print("[done]")


if __name__ == "__main__":
    main()
