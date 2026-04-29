#!/usr/bin/env python3

import os
import pandas as pd
import matplotlib.pyplot as plt


CSV_PATH = "artifacts/data/perf_stat_basics.csv"
PLOT_DIR = "plots"


def ensure_plot_dir():
    os.makedirs(PLOT_DIR, exist_ok=True)


def load_data():
    if not os.path.exists(CSV_PATH):
        raise FileNotFoundError(f"CSV file not found: {CSV_PATH}")

    df = pd.read_csv(CSV_PATH)
    return df


def plot_ns_per_iter(df):
    plt.figure(figsize=(10, 6))
    plt.bar(df["mode"], df["ns_per_iter"])
    plt.yscale("log")
    plt.xlabel("Mode")
    plt.ylabel("ns per iteration (log scale)")
    plt.title("perf stat basics: runtime per iteration")
    plt.xticks(rotation=30, ha="right")
    plt.tight_layout()
    plt.savefig(os.path.join(PLOT_DIR, "01_ns_per_iter.png"), dpi=160)
    plt.close()


def plot_elapsed_time(df):
    plt.figure(figsize=(10, 6))
    plt.bar(df["mode"], df["elapsed_ns"] / 1e9)
    plt.xlabel("Mode")
    plt.ylabel("Elapsed time (seconds)")
    plt.title("perf stat basics: elapsed time")
    plt.xticks(rotation=30, ha="right")
    plt.tight_layout()
    plt.savefig(os.path.join(PLOT_DIR, "02_elapsed_seconds.png"), dpi=160)
    plt.close()


def plot_compute_modes(df):
    compute_modes = [
        "dep_add",
        "indep_add",
        "branch_predictable",
        "branch_unpredictable",
    ]

    sub = df[df["mode"].isin(compute_modes)]

    plt.figure(figsize=(10, 6))
    plt.bar(sub["mode"], sub["ns_per_iter"])
    plt.xlabel("Mode")
    plt.ylabel("ns per iteration")
    plt.title("compute-heavy modes: cost per iteration")
    plt.xticks(rotation=30, ha="right")
    plt.tight_layout()
    plt.savefig(os.path.join(PLOT_DIR, "03_compute_modes_ns_per_iter.png"), dpi=160)
    plt.close()


def plot_syscall_vs_compute(df):
    selected_modes = [
        "branch_predictable",
        "branch_unpredictable",
        "syscall_getpid",
    ]

    sub = df[df["mode"].isin(selected_modes)]

    plt.figure(figsize=(10, 6))
    plt.bar(sub["mode"], sub["ns_per_iter"])
    plt.yscale("log")
    plt.xlabel("Mode")
    plt.ylabel("ns per iteration (log scale)")
    plt.title("branch cost vs syscall boundary cost")
    plt.xticks(rotation=30, ha="right")
    plt.tight_layout()
    plt.savefig(os.path.join(PLOT_DIR, "04_branch_vs_syscall.png"), dpi=160)
    plt.close()


def main():
    ensure_plot_dir()
    df = load_data()

    plot_ns_per_iter(df)
    plot_elapsed_time(df)
    plot_compute_modes(df)
    plot_syscall_vs_compute(df)

    print(f"[done] plots written to: {PLOT_DIR}/")


if __name__ == "__main__":
    main()
