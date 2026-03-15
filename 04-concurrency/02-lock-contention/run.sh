#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT_DIR"

mkdir -p artifacts/bin artifacts/data

make

OUT_CSV="artifacts/data/lock_contention.csv"

cat > "$OUT_CSV" << 'EOF'
threads,iters_per_thread,cs_work,outside_work,pin_cpu,elapsed_ns,total_ops,ns_per_op,mops_per_sec,final_counter,sink
EOF

PIN_CPU="${PIN_CPU:-1}"
ITERS="${ITERS:-1000000}"

# sweep parameters
THREADS_LIST="${THREADS_LIST:-1 2 4 8}"
CS_WORK_LIST="${CS_WORK_LIST:-0 50 200}"
OUTSIDE_WORK_LIST="${OUTSIDE_WORK_LIST:-0 200 1000}"

for threads in $THREADS_LIST; do
	for cs_work in $CS_WORK_LIST; do
		for outside_work in $OUTSIDE_WORK_LIST; do
			echo "[run] threads=${threads} iters=${ITERS} cs_work=${cs_work} outside_work=${outside_work} pin=${PIN_CPU}"

			line="$(
			./artifacts/bin/lock_contention \
				"$threads" \
				"$ITERS" \
				"$cs_work" \
				"$outside_work" \
				"$PIN_CPU"
			)"

			echo "  $line"

			echo "$line" | awk -F',' '
			{
				for (i = 1; i<= NF; i++) {
					split($i, kv, "=")
					vals[i] = kv[2]
				}
			printf "%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n",
			vals[1], vals[2], vals[3], vals[4], vals[5],
			vals[6], vals[7], vals[8], vals[9], vals[10], vals[11]
		}
	' >> "$OUT_CSV"
done
done
done

echo
echo "[done] wrote $OUT_CSV"
