#!/usr/bin/env bash
# Experiment 5.1: Benefits of QuART
#
# Runs ART, QuART_tail, QuART_lil, and QuART_stail over the full K-L
# sortedness grid (N=500M).  Output corresponds to Figures 7 and 8 in the
# paper: fast-path insert distributions and insertion speedup heatmaps.
#
# Usage:
#   bash experiments/5.1-quart-benefits/run.sh
#
# Environment variables (all optional):
#   WORKLOAD_DIR  – directory containing BoDS workload .bin files
#                   (default: /scratch/cgokmen/bods/workloads)
#   REPEAT        – number of timed repetitions per configuration (default: 1)
#                   (set to 5 to reproduce the paper's numbers; substantially longer)
#
# Output:
#   experiments/5.1-quart-benefits/results/results_<TIMESTAMP>.csv
#
# CSV columns:
#   workload, K, L, tree_type, avg_insert_ns, avg_query_ns
#
#   K=0,L=0 is the fully sorted row (bottom row of Figure 8).
#   K ∈ {1,5,10,25,100} × L ∈ {1,5,10,25,100} cover the rest of the grid.
#   avg_insert_ns / avg_query_ns are nanoseconds averaged over REPEAT runs.
#   Throughput = N / (time_ns / 1e9)  ops/sec.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD="$REPO_ROOT/build"

# Bootstrap: clone deps, generate workloads, build binaries (idempotent)
source "$SCRIPT_DIR/../setup.sh"

# setup.sh exports WORKLOAD_DIR; allow override via environment
WORKLOAD_DIR="${WORKLOAD_DIR:-$WORKLOAD_DIR}"
REPEAT="${REPEAT:-1}"  # paper used 5; increase for publication-quality averages
N=500000000

SUFFIX=$(date +"%Y%m%d_%H%M%S")
RESULTS_DIR="$SCRIPT_DIR/results"
RESULTS_FILE="$RESULTS_DIR/results_${SUFFIX}.csv"
LOG_DIR="$RESULTS_DIR/logs_${SUFFIX}"

mkdir -p "$RESULTS_DIR" "$LOG_DIR"

echo "Experiment 5.1 — Benefits of QuART"
echo "  WORKLOAD_DIR : $WORKLOAD_DIR"
echo "  REPEAT       : $REPEAT"
echo "  Results      : $RESULTS_FILE"
echo "  Logs         : $LOG_DIR"
echo ""

# ---------------------------------------------------------------------------
# CSV header
# ---------------------------------------------------------------------------
echo "workload,K,L,tree_type,avg_insert_ns,avg_query_ns" > "$RESULTS_FILE"

TREES=(ART QuART_tail QuART_lil QuART_stail)

# ---------------------------------------------------------------------------
# Helper: run one (workload, tree) configuration REPEAT times and average
# ---------------------------------------------------------------------------
run_config() {
    local FILE="$1" N_VAL="$2" K_VAL="$3" L_VAL="$4" TREE="$5"
    local WNAME LOG INSERT_SUM QUERY_SUM FAILED
    WNAME="$(basename "$FILE" .bin)"
    LOG="$LOG_DIR/${WNAME}_${TREE}.log"
    INSERT_SUM=0
    QUERY_SUM=0
    FAILED=0

    echo "=== workload=$WNAME  tree=$TREE ===" > "$LOG"

    for ((i=1; i<=REPEAT; i++)); do
        echo "--- Run $i/$REPEAT ---" >> "$LOG"
        OUTPUT=$("$BUILD/run" -f "$FILE" -N "$N_VAL" -t "$TREE" 2>>"$LOG") || true
        STATUS=$?
        echo "$OUTPUT" >> "$LOG"

        if [[ $STATUS -ne 0 || -z "$OUTPUT" ]]; then
            echo "  [WARN] run failed: K=$K_VAL L=$L_VAL tree=$TREE run=$i" >&2
            FAILED=1
            continue
        fi

        CSV_LINE=$(echo "$OUTPUT" | tail -1)
        INSERT_SUM=$((INSERT_SUM + $(echo "$CSV_LINE" | cut -d',' -f1 | xargs)))
        QUERY_SUM=$((QUERY_SUM  + $(echo "$CSV_LINE" | cut -d',' -f2 | xargs)))
    done

    if [[ $FAILED -eq 0 ]]; then
        AVG_INS=$((INSERT_SUM / REPEAT))
        AVG_QRY=$((QUERY_SUM  / REPEAT))
        echo "$WNAME,$K_VAL,$L_VAL,$TREE,$AVG_INS,$AVG_QRY" >> "$RESULTS_FILE"
        printf "  %-15s  avg_insert=%dns  avg_query=%dns\n" "$TREE" "$AVG_INS" "$AVG_QRY"
    else
        echo "$WNAME,$K_VAL,$L_VAL,$TREE,ERROR,ERROR" >> "$RESULTS_FILE"
    fi
}

# ---------------------------------------------------------------------------
# K=0, L=0 — fully sorted (bottom row of Figure 8)
# ---------------------------------------------------------------------------
FILE="$WORKLOAD_DIR/workload_N${N}_K0_L0.bin"
if [[ -f "$FILE" ]]; then
    echo ">>> K=0  L=0  (fully sorted)"
    for TREE in "${TREES[@]}"; do
        run_config "$FILE" "$N" 0 0 "$TREE"
    done
else
    echo "[SKIP] Missing: $FILE" >&2
fi

# ---------------------------------------------------------------------------
# K ∈ {1,5,10,25,100}  ×  L ∈ {1,5,10,25,100}
# ---------------------------------------------------------------------------
for K in 1 5 10 25 100; do
    for L in 1 5 10 25 100; do
        FILE="$WORKLOAD_DIR/workload_N${N}_K${K}_L${L}.bin"
        if [[ -f "$FILE" ]]; then
            echo ">>> K=$K  L=$L"
            for TREE in "${TREES[@]}"; do
                run_config "$FILE" "$N" "$K" "$L" "$TREE"
            done
        else
            echo "[SKIP] Missing: $FILE" >&2
        fi
    done
done

echo ""
echo "Done. Results saved to $RESULTS_FILE"
