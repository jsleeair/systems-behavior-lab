#!/usr/bin/env python3
from pathlib import Path

import pandas as pd
import matplotlib.pyplot as plt


ROOT = Path(__file__).resolve().parent
DATA_CSV = ROOT / "artifacts" / "data" / "cow.csv"
PLOTS_DIR = ROOT / "plots"


def load_data() -> pd.DataFrame:
    if not DATA_CSV.exists():
        raise FileNotFoundError(f"CSV not found: {DATA_CSV}")

    df = pd.read_csv(DATA_CSV)

    expected_cols = {
        "mode",
        "pages",
        "page_size",
        "total_bytes",
        "warmup",
        "pin_cpu",
        "elapsed_ns",
        "minflt_delta",
        "majflt_delta",
        "ns_per_page",
    }
    missing = expected_cols - set(df.columns)
    if missing:
        raise ValueError(f"Missing columns in CSV: {sorted(missing)}")

    df = df.sort_values(["pages", "mode"]).reset_index(drop=True)
    return df


def ensure_plots_dir() -> None:
    PLOTS_DIR.mkdir(parents=True, exist_ok=True)


def save_minflt_plot(df: pd.DataFrame) -> None:
    plt.figure(figsize=(8, 5))

    for mode in ["read", "write"]:
        sub = df[df["mode"] == mode].sort_values("pages")
        plt.plot(sub["pages"], sub["minflt_delta"], marker="o", label=mode)

    plt.xscale("log", base=2)
    plt.xlabel("Pages touched")
    plt.ylabel("Minor faults during child access")
    plt.title("Copy-on-Write: minor page faults vs pages")
    plt.grid(True, alpha=0.3)
    plt.legend()
    plt.tight_layout()
    plt.savefig(PLOTS_DIR / "cow_minflt_vs_pages.png", dpi=200)
    plt.close()


def save_ns_per_page_plot(df: pd.DataFrame) -> None:
    plt.figure(figsize=(8, 5))

    for mode in ["read", "write"]:
        sub = df[df["mode"] == mode].sort_values("pages")
        plt.plot(sub["pages"], sub["ns_per_page"], marker="o", label=mode)

    plt.xscale("log", base=2)
    plt.xlabel("Pages touched")
    plt.ylabel("Nanoseconds per page")
    plt.title("Copy-on-Write: ns/page vs pages")
    plt.grid(True, alpha=0.3)
    plt.legend()
    plt.tight_layout()
    plt.savefig(PLOTS_DIR / "cow_ns_per_page_vs_pages.png", dpi=200)
    plt.close()


def save_elapsed_plot(df: pd.DataFrame) -> None:
    plt.figure(figsize=(8, 5))

    for mode in ["read", "write"]:
        sub = df[df["mode"] == mode].sort_values("pages")
        plt.plot(sub["pages"], sub["elapsed_ns"], marker="o", label=mode)

    plt.xscale("log", base=2)
    plt.yscale("log", base=10)
    plt.xlabel("Pages touched")
    plt.ylabel("Elapsed time (ns)")
    plt.title("Copy-on-Write: elapsed time vs pages")
    plt.grid(True, alpha=0.3)
    plt.legend()
    plt.tight_layout()
    plt.savefig(PLOTS_DIR / "cow_elapsed_vs_pages.png", dpi=200)
    plt.close()


def save_ratio_plot(df: pd.DataFrame) -> None:
    pivot = df.pivot(index="pages", columns="mode", values="ns_per_page").reset_index()
    pivot = pivot.sort_values("pages")

    if "read" not in pivot.columns or "write" not in pivot.columns:
        return

    pivot["write_read_ratio"] = pivot["write"] / pivot["read"]

    plt.figure(figsize=(8, 5))
    plt.plot(pivot["pages"], pivot["write_read_ratio"], marker="o")

    plt.xscale("log", base=2)
    plt.xlabel("Pages touched")
    plt.ylabel("Write / Read ns_per_page")
    plt.title("Copy-on-Write: per-page cost ratio")
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    plt.savefig(PLOTS_DIR / "cow_write_read_ratio.png", dpi=200)
    plt.close()


def print_text_summary(df: pd.DataFrame) -> None:
    print("[summary] by mode")
    summary = (
        df.groupby("mode")[["elapsed_ns", "minflt_delta", "majflt_delta", "ns_per_page"]]
        .agg(["mean", "min", "max"])
        .round(2)
    )
    print(summary)
    print()

    print("[summary] write-side fault overhead relative to pages")
    write_df = df[df["mode"] == "write"].copy().sort_values("pages")
    write_df["extra_faults"] = write_df["minflt_delta"] - write_df["pages"]
    print(write_df[["pages", "minflt_delta", "extra_faults", "elapsed_ns", "ns_per_page"]].to_string(index=False))
    print()

    if {"read", "write"} <= set(df["mode"].unique()):
        pivot = df.pivot(index="pages", columns="mode", values="ns_per_page").reset_index()
        pivot = pivot.sort_values("pages")
        pivot["write_read_ratio"] = pivot["write"] / pivot["read"]
        print("[summary] write/read ns_per_page ratio")
        print(pivot[["pages", "write_read_ratio"]].round(2).to_string(index=False))


def main() -> None:
    ensure_plots_dir()
    df = load_data()

    save_minflt_plot(df)
    save_ns_per_page_plot(df)
    save_elapsed_plot(df)
    save_ratio_plot(df)
    print_text_summary(df)

    print()
    print(f"[done] saved plots to: {PLOTS_DIR}")


if __name__ == "__main__":
    main()
