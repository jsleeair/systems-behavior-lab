import pandas as pd
import matplotlib.pyplot as plt
from pathlib import Path

DATA = "artifacts/data/prefetch_locality.csv"
PLOT_DIR = Path("artifacts/plots")
PLOT_DIR.mkdir(parents=True, exist_ok=True)

df = pd.read_csv(DATA)

# working sets to visualize
WORKING_SETS = [
    (32768, "32KB (L1-sized)"),
    (1048576, "1MB (L2/L3 region)"),
    (67108864, "64MB (DRAM)")
]

for size, label in WORKING_SETS:

    subset = df[df["bytes"] == size]

    seq = subset[subset["pattern"] == "seq"]
    rand = subset[subset["pattern"] == "random"]

    plt.figure()

    plt.plot(seq["stride_bytes"], seq["ns_per_access"],
             marker="o", label="sequential")

    plt.plot(rand["stride_bytes"], rand["ns_per_access"],
             marker="o", label="random")

    plt.xscale("log")

    plt.xlabel("Stride (bytes)")
    plt.ylabel("ns per access")
    plt.title(f"Prefetch & Locality Effect ({label})")

    plt.legend()
    plt.grid(True)

    outfile = PLOT_DIR / f"prefetch_{size}.png"
    plt.savefig(outfile, dpi=200)
    plt.close()

print("plots written to:", PLOT_DIR)
