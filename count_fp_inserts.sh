#!/bin/bash
set -euo pipefail

TS=$(date +"%Y%m%d_%H%M%S")
OUTDIR="results"
mkdir -p "$OUTDIR"
RESULTS="${OUTDIR}/fp_inserts_${TS}.csv"

# Ensure binary exists
BIN="./build/run_count_inserts"
if [ ! -x "$BIN" ]; then
  echo "Error: $BIN not found or not executable" >&2
  exit 1
fi

# CSV header
echo "N,K,L,number_of_fp_inserts" > "$RESULTS"

for FILE in ../bods/workloads/workload_N*_K*_L*.bin; do
  [ -f "$FILE" ] || continue

  BASENAME=$(basename "$FILE")
  N=$(echo "$BASENAME" | sed -n 's/.*_N\([0-9]*\)_K[0-9]*_L[0-9]*\.bin/\1/p')
  K=$(echo "$BASENAME" | sed -n 's/.*_N[0-9]*_K\([0-9]*\)_L[0-9]*\.bin/\1/p')
  L=$(echo "$BASENAME" | sed -n 's/.*_N[0-9]*_K[0-9]*_L\([0-9]*\)\.bin/\1/p')

  if [ -z "$N" ] || [ -z "$K" ] || [ -z "$L" ]; then
    echo "Skip (parse failed): $BASENAME" >&2
    continue
  fi

  # Skip K=100 workloads
  if [ "$K" -eq 100 ]; then
    echo "Skip (K=100): $BASENAME"
    continue
  fi

  OUTPUT=$("$BIN" -f "$FILE" 2>&1) || {
    echo "Run failed: $BASENAME -> $OUTPUT" >&2
    continue
  }

  FP_INSERTS_COUNT=$(echo "$OUTPUT" | grep -Eo '^[0-9]+$' || echo "")
  if [ -z "$FP_INSERTS_COUNT" ]; then
    FP_INSERTS_COUNT=$(echo "$OUTPUT" | grep -Eo '[0-9]+' | tail -n1 || echo "")
  fi
  if [ -z "$FP_INSERTS_COUNT" ]; then
    echo "No numeric fp inserts found in output for $BASENAME: $OUTPUT" >&2
    continue
  fi

  echo "${N},${K},${L},${FP_INSERTS_COUNT}" >> "$RESULTS"
  echo "OK: N=$N K=$K L=$L fp_inserts=$FP_INSERTS_COUNT"
done

echo "Done: $RESULTS"