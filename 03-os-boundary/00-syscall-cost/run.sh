#!/usr/bin/env bash
set -euo pipefail

ITERS="${ITERS:-10000000}"
PIN="${PIN:-1}"
REPEATS="${REPEATS:-5}"

mkdir -p artifacts/bin artifacts/data

echo "iters=${ITERS} pin=${PIN} repeats=${REPEATS}"
echo "run,mode,iters,pin,elapsed_ns,ns_per_iter,sink" > artifacts/data/syscall_cost.csv

for r in $(seq 1 "${REPEATS}"); do
    echo "[run ${r}/${REPEATS}]"

    output="$(artifacts/bin/syscall_cost "${ITERS}" "${PIN}")"
    echo "${output}"

    while IFS= read -r line; do
        mode="$(echo "${line}" | awk '{for(i=1;i<=NF;i++) if($i ~ /^mode=/){split($i,a,"="); print a[2]}}')"
        iters_val="$(echo "${line}" | awk '{for(i=1;i<=NF;i++) if($i ~ /^iters=/){split($i,a,"="); print a[2]}}')"
        pin_val="$(echo "${line}" | awk '{for(i=1;i<=NF;i++) if($i ~ /^pin=/){split($i,a,"="); print a[2]}}')"
        elapsed_ns="$(echo "${line}" | awk '{for(i=1;i<=NF;i++) if($i ~ /^elapsed_ns=/){split($i,a,"="); print a[2]}}')"
        ns_per_iter="$(echo "${line}" | awk '{for(i=1;i<=NF;i++) if($i ~ /^ns_per_iter=/){split($i,a,"="); print a[2]}}')"
        sink="$(echo "${line}" | awk '{for(i=1;i<=NF;i++) if($i ~ /^sink=/){split($i,a,"="); print a[2]}}')"

        echo "${r},${mode},${iters_val},${pin_val},${elapsed_ns},${ns_per_iter},${sink}" \
            >> artifacts/data/syscall_cost.csv
    done <<< "${output}"
done

echo
echo "[saved] artifacts/data/syscall_cost.csv"
