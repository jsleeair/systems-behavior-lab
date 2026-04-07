#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT_DIR"

echo "[build]"
make

mkdir -p artifacts/data

OUT="artifacts/data/producer_consumer_queue.csv"

./artifacts/bin/producer_consumer_queue --csv-header > "$OUT"

echo "[run] writing to $OUT"

for capacity in 64 256 1024 4096; do
	for consumer_work in 0 100 1000; do
		./artifacts/bin/producer_consumer_queue \
			--producers 2 \
			--consumers 2 \
			--items-per-producer 500000 \
			--capacity "$capacity" \
			--producer-work 0 \
			--consumer-work "$consumer_work" \
			--warmup 1 \
			--repeats 3 \
			--pin-cpu 0 \
			--csv >> "$OUT"
		done
	done

for producers in 1 2 4; do
  for consumers in 1 2 4; do
    ./artifacts/bin/producer_consumer_queue \
      --producers "$producers" \
      --consumers "$consumers" \
      --items-per-producer 300000 \
      --capacity 256 \
      --producer-work 0 \
      --consumer-work 100 \
      --warmup 1 \
      --repeats 3 \
      --pin-cpu 0 \
      --csv >> "$OUT"
  done
done
echo "[done] output: $OUT"
