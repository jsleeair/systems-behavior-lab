import os
import pandas as pd
import matplotlib.pyplot as plt

# --- Paths ---
ROOT_DIR = os.path.dirname(os.path.abspath(__file__))
DATA_PATH = os.path.join(ROOT_DIR, "artifacts", "data", "ring_buffer.csv")
PLOTS_DIR = os.path.join(ROOT_DIR, "plots")

os.makedirs(PLOTS_DIR, exist_ok=True)


# --- Load data ---
df = pd.read_csv(DATA_PATH)

# Ensure numeric
df["capacity"] = df["capacity"].astype(int)
df["ns_per_msg"] = df["ns_per_msg"].astype(float)
df["full_hits"] = df["full_hits"].astype(int)

# Aggregate (mean over repeats)
grouped = (
    df.groupby(["mode", "capacity"])
    .agg(
        ns_per_msg_mean=("ns_per_msg", "mean"),
        full_hits_mean=("full_hits", "mean"),
    )
    .reset_index()
)

print("[summary] grouped data:")
print(grouped.head())


# --- Plot 1: Throughput (ns_per_msg) ---
plt.figure()

for mode in grouped["mode"].unique():
    sub = grouped[grouped["mode"] == mode]
    plt.plot(sub["capacity"], sub["ns_per_msg_mean"], marker="o", label=mode)

plt.xscale("log", base=2)
plt.xlabel("Capacity (log2 scale)")
plt.ylabel("ns per message")
plt.title("Ring Buffer Throughput vs Capacity")
plt.legend()
plt.grid(True)

out1 = os.path.join(PLOTS_DIR, "throughput_vs_capacity.png")
plt.savefig(out1)
plt.close()

print(f"[plot] saved: {out1}")


# --- Plot 2: Full hits (backpressure) ---
plt.figure()

for mode in grouped["mode"].unique():
    sub = grouped[grouped["mode"] == mode]
    plt.plot(sub["capacity"], sub["full_hits_mean"], marker="o", label=mode)

plt.xscale("log", base=2)
plt.xlabel("Capacity (log2 scale)")
plt.ylabel("Full hits (spin count)")
plt.title("Backpressure (Full Hits) vs Capacity")
plt.legend()
plt.grid(True)

out2 = os.path.join(PLOTS_DIR, "full_hits_vs_capacity.png")
plt.savefig(out2)
plt.close()

print(f"[plot] saved: {out2}")
