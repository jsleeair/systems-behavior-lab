import os
import pandas as pd
import matplotlib.pyplot as plt

DATA_PATH = "artifacts/data/thread_pool_scaling.csv"
PLOT_DIR = "plots"


def ensure_plot_dir():
    if not os.path.exists(PLOT_DIR):
        os.makedirs(PLOT_DIR)


def load_data():
    df = pd.read_csv(DATA_PATH)
    return df


def aggregate(df):
    grouped = (
        df.groupby(["threads", "task_iters"])
        .agg(
            mean_tasks_per_sec=("tasks_per_sec", "mean"),
            mean_elapsed_ns=("elapsed_ns", "mean"),
        )
        .reset_index()
    )
    return grouped


def compute_speedup(df):
    result = df.copy()

    baseline = (
        df[df["threads"] == 1]
        .set_index("task_iters")["mean_elapsed_ns"]
        .to_dict()
    )

    def speedup(row):
        return baseline[row["task_iters"]] / row["mean_elapsed_ns"]

    result["speedup"] = result.apply(speedup, axis=1)
    return result


def compute_efficiency(df):
    df = df.copy()
    df["efficiency"] = df["speedup"] / df["threads"]
    return df


def plot_throughput(df):
    plt.figure()

    for task_iters in sorted(df["task_iters"].unique()):
        sub = df[df["task_iters"] == task_iters]
        plt.plot(
            sub["threads"],
            sub["mean_tasks_per_sec"],
            marker="o",
            label=f"iters={task_iters}",
        )

    plt.xlabel("Threads")
    plt.ylabel("Tasks per second")
    plt.title("Throughput vs Threads")
    plt.legend()
    plt.grid()

    plt.savefig(f"{PLOT_DIR}/throughput_vs_threads.png", dpi=200)
    plt.close()


def plot_speedup(df):
    plt.figure()

    for task_iters in sorted(df["task_iters"].unique()):
        sub = df[df["task_iters"] == task_iters]
        plt.plot(
            sub["threads"],
            sub["speedup"],
            marker="o",
            label=f"iters={task_iters}",
        )

    plt.xlabel("Threads")
    plt.ylabel("Speedup")
    plt.title("Speedup vs Threads")
    plt.legend()
    plt.grid()

    plt.savefig(f"{PLOT_DIR}/speedup_vs_threads.png", dpi=200)
    plt.close()


def plot_efficiency(df):
    plt.figure()

    for task_iters in sorted(df["task_iters"].unique()):
        sub = df[df["task_iters"] == task_iters]
        plt.plot(
            sub["threads"],
            sub["efficiency"],
            marker="o",
            label=f"iters={task_iters}",
        )

    plt.xlabel("Threads")
    plt.ylabel("Efficiency")
    plt.title("Efficiency vs Threads")
    plt.legend()
    plt.grid()

    plt.savefig(f"{PLOT_DIR}/efficiency_vs_threads.png", dpi=200)
    plt.close()


def plot_pinning_compare():
    files = {
        "pin0": "artifacts/data/thread_pool_scaling_pin0.csv",
        "pin1": "artifacts/data/thread_pool_scaling_pin1.csv",
    }

    plt.figure()

    for label, path in files.items():
        if not os.path.exists(path):
            continue

        df = pd.read_csv(path)

        grouped = (
            df[df["task_iters"] == 1000]
            .groupby("threads")
            .agg(mean_tasks_per_sec=("tasks_per_sec", "mean"))
            .reset_index()
        )

        plt.plot(
            grouped["threads"],
            grouped["mean_tasks_per_sec"],
            marker="o",
            label=label,
        )

    plt.xlabel("Threads")
    plt.ylabel("Tasks per second")
    plt.title("Pinning Comparison (task_iters=1000)")
    plt.legend()
    plt.grid()

    plt.savefig(f"{PLOT_DIR}/pinning_compare_1000.png", dpi=200)
    plt.close()


def main():
    ensure_plot_dir()

    df = load_data()
    agg = aggregate(df)

    speed = compute_speedup(agg)
    eff = compute_efficiency(speed)

    plot_throughput(agg)
    plot_speedup(speed)
    plot_efficiency(eff)
    plot_pinning_compare()

    print("plots generated in ./plots")


if __name__ == "__main__":
    main()
