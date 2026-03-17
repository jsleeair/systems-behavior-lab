#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

mkdir -p artifacts/bin artifacts/data

make all

OUT="artifacts/data/thread_pool_scaling.csv"

echo "threads,tasks,task_iters,warmup_rounds,pin_workers,queue_capacity,elapsed_ns,ns_per_task,tasks_per_sec,checksum" > "$OUT"

# You can tune these to match your machine.
THREADS_LIST=(1 2 4 8)
TASK_ITERS_LIST=(100 1000 10000 100000)

TASKS="${TASKS:-20000}"
WARMUP="${WARMUP:-1}"
PIN_WORKERS="${PIN_WORKERS:-1}"
QUEUE_CAPACITY="${QUEUE_CAPACITY:-1024}"
REPEATS="${REPEATS:-3}"

for task_iters in "${TASK_ITERS_LIST[@]}"; do
  for threads in "${THREADS_LIST[@]}"; do
    for ((r=1; r<=REPEATS; r++)); do
      line="$(
        ./artifacts/bin/thread_pool_scaling \
          --threads "$threads" \
          --tasks "$TASKS" \
          --task-iters "$task_iters" \
          --warmup "$WARMUP" \
          --pin-workers "$PIN_WORKERS" \
          --queue-capacity "$QUEUE_CAPACITY"
      )"

      # Convert key=value,key=value,... into CSV row.
      csv_row="$(echo "$line" | awk -F, '
        {
          for (i = 1; i <= NF; i++) {
            split($i, a, "=")
            printf "%s", a[2]
            if (i < NF) printf ","
          }
          printf "\n"
        }'
      )"

      echo "$csv_row" >> "$OUT"
      echo "[run] $line"
    done
  done
done

echo
echo "Saved results to: $OUT"
