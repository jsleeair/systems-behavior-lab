#!/usr/bin/env bash

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

echo "[RUN] Building all steps and collecting artifacts..."
make -s all

echo
echo "[SUMMARY]"
for step in step1-multiple-definition \
            step2-fix-header-declare \
            step3-static-in-header \
            step4-static-inline-in-header
do
  echo "== $step =="

  if [[ -f "$step/app" ]]; then
    echo "  app: OK"
  else
    echo "  app: (no binary)  <-- expected for step1"
  fi

  if [[ -s "$step/artifacts/link.stderr" ]]; then
    echo "  link.stderr: present (first lines):"
    head -n 4 "$step/artifacts/link.stderr" | sed 's/^/    /'
  else
    echo "  link.stderr: empty"
  fi

  echo "  artifacts:"
  ls -1 "$step/artifacts" 2>/dev/null | sed 's/^/    /'
  echo
done

echo "[DONE]"

