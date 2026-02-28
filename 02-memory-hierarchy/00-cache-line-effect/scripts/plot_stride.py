#!/usr/bin/env python3
import csv
import sys
import matplotlib.pyplot as plt

if len(sys.argv) != 3:
    print("Usage: plot_stride.py <csv> <outdir>")
    sys.exit(1)

csv_path = sys.argv[1]
outdir = sys.argv[2]

strides_sparse = []
use_sparse = []
util_sparse = []

strides_dense = []
use_dense = []
util_dense = []

with open(csv_path) as f:
    reader = csv.DictReader(f)
    for row in reader:
        stride = int(row["stride"])
        useful = float(row["useful_GBps"])
        traffic = float(row["traffic_GBps"])
        util = useful / traffic if traffic > 0 else 0

        if row["mode"] == "sparse":
            strides_sparse.append(stride)
            use_sparse.append(useful)
            util_sparse.append(util)
        else:
            strides_dense.append(stride)
            use_dense.append(useful)
            util_dense.append(util)

# -------------------------
# Graph 1: Useful Throughput
# -------------------------
plt.figure()
plt.plot(strides_sparse, use_sparse, marker='o', label="sparse")
plt.plot(strides_dense, use_dense, marker='o', label="dense")

plt.axvline(x=64, linestyle='--')
plt.text(64, max(use_dense)*0.9, "64B cache line", rotation=90)

plt.xscale("log", base=2)
plt.xlabel("Stride (bytes)")
plt.ylabel("Useful Throughput (GB/s)")
plt.title("Stride vs Useful Throughput")
plt.legend()
plt.grid(True)
plt.tight_layout()
plt.savefig(f"{outdir}/stride_useful.png")
plt.close()

# -------------------------
# Graph 2: Utilization
# -------------------------
plt.figure()
plt.plot(strides_sparse, util_sparse, marker='o', label="sparse")
plt.plot(strides_dense, util_dense, marker='o', label="dense")

plt.axvline(x=64, linestyle='--')
plt.text(64, 0.8, "64B cache line", rotation=90)

plt.xscale("log", base=2)
plt.xlabel("Stride (bytes)")
plt.ylabel("Cache Line Utilization")
plt.title("Stride vs Cache Line Utilization")
plt.legend()
plt.grid(True)
plt.tight_layout()
plt.savefig(f"{outdir}/stride_utilization.png")
plt.close()
