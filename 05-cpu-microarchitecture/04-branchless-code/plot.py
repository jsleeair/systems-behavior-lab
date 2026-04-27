#!/usr/bin/env python3
import os
import pandas as pd
import matplotlib.pyplot as plt

# Paths
DATA_PATH = "artifacts/data/branchless_code.csv"
PLOTS_DIR = "plots"

os.makedirs(PLOTS_DIR, exist_ok=True)

# Load CSV
df = pd.read_csv(DATA_PATH)

# Convert ns -> ms for readability
df["elapsed_ms"] = df["elapsed_ns"] / 1e6

# -----------------------------
# 1. Pattern vs Mode (Main Plot)
# -----------------------------
pivot = df.pivot(index="pattern", columns="mode", values="elapsed_ms")

plt.figure()
pivot.plot(kind="bar")
plt.title("Branchy vs Branchless (by Pattern)")
plt.ylabel("Elapsed Time (ms)")
plt.xlabel("Pattern")
plt.xticks(rotation=0)
plt.tight_layout()

plt.savefig(f"{PLOTS_DIR}/pattern_vs_mode.png")
plt.close()

# -----------------------------
# 2. Vec vs NoVec Comparison
# (manual input from your results)
# -----------------------------

# You measured these manually (random50 case)
data_vec_novec = {
    "config": [
        "branchy_vec",
        "branchless_vec",
        "branchy_novec",
        "branchless_novec",
    ],
    "elapsed_ms": [
        15.79,
        10.57,
        198.04,
        20.79,
    ],
}

df2 = pd.DataFrame(data_vec_novec)

plt.figure()
plt.bar(df2["config"], df2["elapsed_ms"])
plt.title("Vectorization Impact (random50)")
plt.ylabel("Elapsed Time (ms)")
plt.xticks(rotation=30)
plt.tight_layout()

plt.savefig(f"{PLOTS_DIR}/vec_vs_novec.png")
plt.close()

print(f"[done] plots saved in ./{PLOTS_DIR}/")
