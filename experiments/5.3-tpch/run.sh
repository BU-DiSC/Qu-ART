#!/usr/bin/env bash
# Experiment 5.3: TPC-H workload
#
# Runs B+-tree (BPTree), QuIT, ART, QuART_tail, QuART_lil, and QuART_stail
# on a TPC-H-inspired near-sorted workload derived from the lineitem table
# (N=6M, K≈96.67%, L≈0.1%).  Output corresponds to Figure 10 in the paper:
# insertion and query throughput on the TPC-H workload.
#
# Usage:
#   bash experiments/5.3-tpch/run.sh
#
# Environment variables (all optional):
#   WORKLOAD_FILE  – path to the TPC-H .bin workload file
#                    (default: /scratch/cgokmen/bods/workloads/workload_N6000000_K9667_L01.bin)
#   REPEAT         – number of timed repetitions per configuration (default: 5)
#
# Output:
#   experiments/5.3-tpch/results/results_<TIMESTAMP>.csv
#
# CSV columns:
#   tree_type, avg_insert_ns, avg_query_ns
#
#   Throughput (Figure 10) = N / (time_ns / 1e9)  ops/sec, where N=6000000.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD="$REPO_ROOT/build"
QUIT_BUILD="$REPO_ROOT/experiments/5.2-quart-vs-quit/build"
JOBS="${JOBS:-$(nproc)}"

# Bootstrap: clone deps, generate workloads, build main binaries (idempotent)
source "$SCRIPT_DIR/../setup.sh"

# Build the QuIT binaries (from experiment 5.2) if not already built
if [[ ! -x "$QUIT_BUILD/run_quit_2k" || ! -x "$QUIT_BUILD/run_quit_base_2k" ]]; then
    echo "[5.3] Building QuIT binaries..."
    QUIT_SRC="$REPO_ROOT/experiments/5.2-quart-vs-quit"
    mkdir -p "$QUIT_BUILD"
    cmake -S "$QUIT_SRC" -B "$QUIT_BUILD" -DCMAKE_BUILD_TYPE=Release -Wno-dev
    make -C "$QUIT_BUILD" -j"$JOBS"
fi

WORKLOAD_FILE="${WORKLOAD_FILE:-$WORKLOAD_DIR/workload_N6000000_K9667_L01.bin}"
REPEAT="${REPEAT:-5}"
N=6000000

SUFFIX=$(date +"%Y%m%d_%H%M%S")
RESULTS_DIR="$SCRIPT_DIR/results"
RESULTS_FILE="$RESULTS_DIR/results_${SUFFIX}.csv"
LOG_DIR="$RESULTS_DIR/logs_${SUFFIX}"

mkdir -p "$RESULTS_DIR" "$LOG_DIR"

echo "Experiment 5.3 — TPC-H workload"
echo "  WORKLOAD_FILE : $WORKLOAD_FILE"
echo "  N             : $N"
echo "  REPEAT        : $REPEAT"
echo "  Results       : $RESULTS_FILE"
echo "  Logs          : $LOG_DIR"
echo ""

# ---------------------------------------------------------------------------
# CSV header
# ---------------------------------------------------------------------------
echo "tree_type,avg_insert_ns,avg_query_ns" > "$RESULTS_FILE"

# ---------------------------------------------------------------------------
# Helper: run a configuration REPEAT times and append average to CSV
# ---------------------------------------------------------------------------
run_config() {
    local BINARY="$1" TREE="$2"
    local LOG="$LOG_DIR/${TREE}.log"
    local INSERT_SUM=0 QUERY_SUM=0 FAILED=0

    # QuIT binaries live in the 5.2 build directory
    local BINARY_PATH
    if [[ "$BINARY" == run_quit* ]]; then
        BINARY_PATH="$QUIT_BUILD/$BINARY"
    else
        BINARY_PATH="$BUILD/$BINARY"
    fi

    echo "=== tree=$TREE ===" > "$LOG"
    echo ">>> $TREE"

    for ((i=1; i<=REPEAT; i++)); do
        echo "--- Run $i/$REPEAT ---" >> "$LOG"
        OUTPUT=$("$BINARY_PATH" -f "$WORKLOAD_FILE" -N "$N" -t "$TREE" 2>>"$LOG") || true
        STATUS=$?
        echo "$OUTPUT" >> "$LOG"

        if [[ $STATUS -ne 0 || -z "$OUTPUT" ]]; then
            echo "  [WARN] run failed: tree=$TREE run=$i" >&2
            FAILED=1
            break
        fi

        CSV_LINE=$(echo "$OUTPUT" | tail -1)
        INSERT_SUM=$((INSERT_SUM + $(echo "$CSV_LINE" | cut -d',' -f1 | xargs)))
        QUERY_SUM=$((QUERY_SUM  + $(echo "$CSV_LINE" | cut -d',' -f2 | xargs)))
    done

    if [[ $FAILED -eq 0 ]]; then
        AVG_INS=$((INSERT_SUM / REPEAT))
        AVG_QRY=$((QUERY_SUM  / REPEAT))
        echo "$TREE,$AVG_INS,$AVG_QRY" >> "$RESULTS_FILE"
        printf "  avg_insert=%dns  avg_query=%dns\n" "$AVG_INS" "$AVG_QRY"
    else
        echo "$TREE,ERROR,ERROR" >> "$RESULTS_FILE"
    fi
}

# ---------------------------------------------------------------------------
# B+-tree baseline (no fast path)
# ---------------------------------------------------------------------------
run_config "run_quit_base_2k" "BPTree"

# ---------------------------------------------------------------------------
# QuIT (Quick Insertion Tree — full fast path)
# ---------------------------------------------------------------------------
run_config "run_quit_2k" "QuIT"

# ---------------------------------------------------------------------------
# ART and QuART variants
# ---------------------------------------------------------------------------
for TREE in ART QuART_tail QuART_lil QuART_stail; do
    run_config "run" "$TREE"
done

echo ""
echo "Done. Results saved to $RESULTS_FILE"
