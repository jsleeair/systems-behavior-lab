#!/usr/bin/env python3

"""
plot.py

Generate plots from branch_prediction.csv

Output:
  plots/
    - ns_per_iter.png
    - elapsed_ns.png

Notes:
  - Do NOT set colors (project rule)
  - Single plot per figure
"""

import os
import pandas as pd
import matplotlib.pyplot as plt

DATA_PATH = "artifacts/data/branch_prediction.csv"
PLOT_DIR = "plots"


def load_data():
    if not os.path.exists(DATA_PATH):
        raise FileNotFoundError(f"CSV not found: {DATA_PATH}")

    df = pd.read_csv(DATA_PATH)

    # Fix potential typo from earlier run
    df["mode"] = df["mode"].replace({
        "alwyas_not_taken": "always_not_taken"
    })

    return df


def ensure_plot_dir():
    os.makedirs(PLOT_DIR, exist_ok=True)


def plot_ns_per_iter(df):
    df_sorted = df.sort_values("ns_per_iter")

    plt.figure()
    plt.bar(df_sorted["mode"], df_sorted["ns_per_iter"])
    plt.xlabel("mode")
    plt.ylabel("ns_per_iter")
    plt.title("Branch Prediction: ns per iteration")
    plt.xticks(rotation=30)

    out = os.path.join(PLOT_DIR, "ns_per_iter.png")
    plt.tight_layout()
    plt.savefig(out, dpi=200)
    plt.close()

    print(f"[saved] {out}")


def plot_elapsed(df):
    df_sorted = df.sort_values("elapsed_ns")

    plt.figure()
    plt.bar(df_sorted["mode"], df_sorted["elapsed_ns"])
    plt.xlabel("mode")
    plt.ylabel("elapsed_ns")
    plt.title("Branch Prediction: total elapsed time")
    plt.xticks(rotation=30)

    out = os.path.join(PLOT_DIR, "elapsed_ns.png")
    plt.tight_layout()
    plt.savefig(out, dpi=200)
    plt.close()

    print(f"[saved] {out}")


def main():
    ensure_plot_dir()
    df = load_data()

    print("[info] loaded data:")
    print(df)

    plot_ns_per_iter(df)
    plot_elapsed(df)


if __name__ == "__main__":
    main()
