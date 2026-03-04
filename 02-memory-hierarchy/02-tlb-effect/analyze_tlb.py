#!/usr/bin/env python3

import pandas as pd

CSV = "artifacts/out/tlb_4k.csv"

df = pd.read_csv(CSV)

pages = df["pages"]
cycles = df["cycles_per_access"]
bytes_ = df["bytes"]

# slope 계산
df["delta"] = cycles.diff()

# 가장 큰 증가 지점
knee_idx = df["delta"].idxmax()

knee_pages = int(pages[knee_idx])
knee_bytes = int(bytes_[knee_idx])
knee_cycles = float(cycles[knee_idx])

baseline_cycles = float(cycles.iloc[0])
max_cycles = float(cycles.max())

increase = max_cycles / baseline_cycles

print("\nTLB ANALYSIS\n")

print(f"knee pages : {knee_pages}")
print(f"knee bytes : {knee_bytes/1024:.1f} KB")

print(f"\nbaseline latency : {baseline_cycles:.2f} cycles")
print(f"max latency      : {max_cycles:.2f} cycles")

print(f"\nlatency increase : {increase:.2f}x")
