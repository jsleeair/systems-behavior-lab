#!/usr/bin/env python3
"""
Plot script for the mmap-vs-read lab.

Reads:
    artifacts/data/results.csv

Writes:
    plots/
"""

from pathlib import Path
import pandas as pd
import matplotlib.pyplot as plt


LAB_ROOT = Path(__file__).resolve().parent
CSV_PATH = LAB_ROOT / "artifacts" / "data" / "results.csv"
PLOTS_DIR = LAB_ROOT / "plots"


def require_input_csv(path: Path) -> None:
    if not path.exists():
        raise FileNotFoundError(
            f"Input CSV not found: {path}\n"
            f"Run the benchmark first, e.g. ./run.sh"
        )


def load_and_prepare_dataframe(path: Path) -> pd.DataFrame:
    df = pd.read_csv(path)

    required_cols = {
        "mode",
        "iter",
        "file_mb",
        "chunk_kb",
        "stride",
        "elapsed_ns",
        "samples",
        "ns_per_sample",
        "mib_per_sec",
        "checksum",
    }
    missing = required_cols - set(df.columns)
    if missing:
        raise ValueError(f"CSV is missing required columns: {sorted(missing)}")

    df = df.copy()
    df["elapsed_ms"] = df["elapsed_ns"] / 1_000_000.0
    df = df.sort_values(by=["chunk_kb", "mode", "iter"]).reset_index(drop=True)
    return df


def summarize(df: pd.DataFrame, value_col: str) -> pd.DataFrame:
    """
    Aggregate per-(mode, chunk_kb) statistics.

    Returns a flat DataFrame with columns:
    - mode
    - chunk_kb
    - mean
    - std
    - min
    - max
    - count
    """
    grouped = (
        df.groupby(["mode", "chunk_kb"], as_index=False)
        .agg(
            mean=(value_col, "mean"),
            std=(value_col, "std"),
            min=(value_col, "min"),
            max=(value_col, "max"),
            count=(value_col, "count"),
        )
        .sort_values(by=["chunk_kb", "mode"])
        .reset_index(drop=True)
    )

    grouped["std"] = grouped["std"].fillna(0.0)
    return grouped


def plot_metric(summary_df: pd.DataFrame, metric_name: str, ylabel: str, out_path: Path) -> None:
    fig, ax = plt.subplots(figsize=(8, 5))

    for mode in ["read", "mmap"]:
        sub = summary_df[summary_df["mode"] == mode].sort_values("chunk_kb")
        if sub.empty:
            continue

        ax.errorbar(
            sub["chunk_kb"],
            sub["mean"],
            yerr=sub["std"],
            marker="o",
            capsize=4,
            label=mode,
        )

    # chunk sizes are usually powers of two, so log2 is the most readable scale
    ax.set_xscale("log", base=2)
    ax.set_xlabel("Read chunk size (KiB)")
    ax.set_ylabel(ylabel)
    ax.set_title(f"{metric_name} vs chunk size")
    ax.grid(True, which="both", alpha=0.3)
    ax.legend()

    unique_chunks = sorted(summary_df["chunk_kb"].unique())
    ax.set_xticks(unique_chunks)
    ax.set_xticklabels([str(v) for v in unique_chunks])

    fig.tight_layout()
    fig.savefig(out_path, dpi=200)
    plt.close(fig)


def print_text_summary(df: pd.DataFrame) -> None:
    print("[summary] mean by mode and chunk size")
    for metric in ["elapsed_ms", "mib_per_sec", "ns_per_sample"]:
        s = summarize(df, metric)
        print(f"\nmetric={metric}")
        for _, row in s.iterrows():
            print(
                f"  mode={row['mode']:<4} "
                f"chunk_kb={int(row['chunk_kb']):>4} "
                f"mean={row['mean']:.2f} "
                f"std={row['std']:.2f} "
                f"n={int(row['count'])}"
            )


def main() -> None:
    require_input_csv(CSV_PATH)
    PLOTS_DIR.mkdir(parents=True, exist_ok=True)

    df = load_and_prepare_dataframe(CSV_PATH)
    print_text_summary(df)

    elapsed_summary = summarize(df, "elapsed_ms")
    throughput_summary = summarize(df, "mib_per_sec")
    ns_per_sample_summary = summarize(df, "ns_per_sample")

    plot_metric(
        elapsed_summary,
        metric_name="Elapsed time",
        ylabel="Elapsed time (ms)",
        out_path=PLOTS_DIR / "mmap_read_elapsed_ms.png",
    )

    plot_metric(
        throughput_summary,
        metric_name="Throughput",
        ylabel="Throughput (MiB/s)",
        out_path=PLOTS_DIR / "mmap_read_throughput_mib_per_sec.png",
    )

    plot_metric(
        ns_per_sample_summary,
        metric_name="Time per sample",
        ylabel="ns/sample",
        out_path=PLOTS_DIR / "mmap_read_ns_per_sample.png",
    )

    print("\n[done] wrote plots:")
    print(f"  {PLOTS_DIR / 'mmap_read_elapsed_ms.png'}")
    print(f"  {PLOTS_DIR / 'mmap_read_throughput_mib_per_sec.png'}")
    print(f"  {PLOTS_DIR / 'mmap_read_ns_per_sample.png'}")


if __name__ == "__main__":
    main()
