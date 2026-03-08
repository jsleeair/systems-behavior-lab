import os
import pandas as pd
import matplotlib.pyplot as plt

# ensure plots directory exists
os.makedirs("plots", exist_ok=True)

# Load CSV
df = pd.read_csv("artifacts/data/syscall_cost.csv")

# Compute mean per mode
means = df.groupby("mode")["ns_per_iter"].mean()

order = [
    "empty_loop",
    "function_call",
    "getpid_wrapper",
    "direct_syscall_getpid",
]

means = means.reindex(order)

plt.figure(figsize=(8,5))
means.plot(kind="bar")

plt.yscale("log")

plt.ylabel("ns per iteration")
plt.title("User-space vs System Call Cost")

plt.xticks(rotation=20)

plt.tight_layout()

plt.savefig("plots/syscall_cost.png", dpi=200)

print("Saved plots/syscall_cost.png")
