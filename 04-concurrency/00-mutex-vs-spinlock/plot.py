import os
import pandas as pd
import matplotlib.pyplot as plt

DATA_PATH = "artifacts/data/mutex_vs_spinlock.csv"
PLOT_DIR = "plots"


def ensure_plot_dir():
    if not os.path.exists(PLOT_DIR):
        os.makedirs(PLOT_DIR)


def load_data():
    df = pd.read_csv(DATA_PATH)
    return df


def plot_threads(df):
    """
    ns/op vs threads
    condition: cs_work=0, outside_work=0
    """
    subset = df[(df["cs_work"] == 0) & (df["outside_work"] == 0)]

    plt.figure()

    for mode in ["mutex", "spin"]:
        s = subset[subset["mode"] == mode]
        s = s.sort_values("threads")
        plt.plot(
            s["threads"],
            s["ns_per_op"],
            marker="o",
            label=mode,
        )

    plt.xlabel("Threads")
    plt.ylabel("ns/op")
    plt.title("Lock Cost vs Threads (cs=0, outside=0)")
    plt.legend()
    plt.grid(True)

    out = os.path.join(PLOT_DIR, "mutex_vs_spin_threads.png")
    plt.savefig(out, dpi=200)
    plt.close()


def plot_cs_work(df):
    """
    ns/op vs cs_work
    condition: threads=8, outside_work=0
    """
    subset = df[(df["threads"] == 8) & (df["outside_work"] == 0)]

    plt.figure()

    for mode in ["mutex", "spin"]:
        s = subset[subset["mode"] == mode]
        s = s.sort_values("cs_work")

        plt.plot(
            s["cs_work"],
            s["ns_per_op"],
            marker="o",
            label=mode,
        )

    plt.xlabel("Critical Section Work")
    plt.ylabel("ns/op")
    plt.title("Lock Cost vs Critical Section Length (threads=8)")
    plt.legend()
    plt.grid(True)

    out = os.path.join(PLOT_DIR, "mutex_vs_spin_cswork.png")
    plt.savefig(out, dpi=200)
    plt.close()


def plot_outside_work(df):
    """
    ns/op vs outside_work
    condition: threads=8, cs_work=0
    """
    subset = df[(df["threads"] == 8) & (df["cs_work"] == 0)]

    plt.figure()

    for mode in ["mutex", "spin"]:
        s = subset[subset["mode"] == mode]
        s = s.sort_values("outside_work")

        plt.plot(
            s["outside_work"],
            s["ns_per_op"],
            marker="o",
            label=mode,
        )

    plt.xlabel("Outside Work")
    plt.ylabel("ns/op")
    plt.title("Lock Cost vs Outside Work (threads=8)")
    plt.legend()
    plt.grid(True)

    out = os.path.join(PLOT_DIR, "mutex_vs_spin_outside.png")
    plt.savefig(out, dpi=200)
    plt.close()


def main():
    ensure_plot_dir()
    df = load_data()

    plot_threads(df)
    plot_cs_work(df)
    plot_outside_work(df)

    print("\n[plots generated]")
    print(" - plots/mutex_vs_spin_threads.png")
    print(" - plots/mutex_vs_spin_cswork.png")
    print(" - plots/mutex_vs_spin_outside.png")


if __name__ == "__main__":
    main()
