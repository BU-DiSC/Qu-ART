#!/usr/bin/env bash
# Run the TPC-C insert experiment for both ART and QuART_stail_reset_2
# and write results to tpcc/results_tpcc_<timestamp>.csv
#
# Usage: bash tpcc/run_tpcc.sh [num_transactions] [repeats] [batch_size]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$ROOT_DIR/build"
BINARY="$BUILD_DIR/tpcc_art"

N=${1:-500000}      # number of NewOrder transactions (~5M keys on average)
REPEATS=${2:-5}     # repeats per tree

TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
RESULTS_DIR="$SCRIPT_DIR/results"
mkdir -p "$RESULTS_DIR"
OUTFILE="$RESULTS_DIR/results_tpcc_${TIMESTAMP}.csv"

# Build if binary missing or sources newer
if [[ ! -f "$BINARY" ]] || \
   [[ "$SCRIPT_DIR/tpcc_art.cpp" -nt "$BINARY" ]] || \
   [[ "$ROOT_DIR/ART.h"       -nt "$BINARY" ]]; then
    echo "Building tpcc_art..."
    cmake --build "$BUILD_DIR" --target tpcc_art
fi

echo "tree_type,workload,num_keys,avg_insert_ns,avg_insert_ns_per_key" > "$OUTFILE"

B=${3:-100}   # batch size, default 100

for TREE in ART QuART_stail_reset_2; do
    echo "Running $TREE  workload=batch(${B})  (N=$N transactions, $REPEATS repeats)..."
    "$BINARY" -N "$N" -t "$TREE" -r "$REPEATS" -v -w batch -B "$B" 2>&1 1>/dev/null | cat
    "$BINARY" -N "$N" -t "$TREE" -r "$REPEATS" -w batch -B "$B" | tee -a "$OUTFILE"
done

echo ""
echo "Results written to $OUTFILE"
