#!/usr/bin/env bash
# Experiment 5.2: Comparing QuART with QuIT
#
# Runs QuART_stail and QuIT_2k over the full K-L sortedness grid (N=500M).
# Output corresponds to Figure 9 in the paper: stail insertion and lookup
# speedup over QuIT across the K-L grid.
#
# Usage:
#   bash experiments/5.2-quart-vs-quit/run.sh
#
# Environment variables (all optional):
#   WORKLOAD_DIR  – directory containing BoDS workload .bin files
#                   (default: /scratch/cgokmen/bods/workloads)
#   REPEAT        – number of timed repetitions per configuration (default: 1)
#                   (set to 5 to reproduce the paper's numbers; substantially longer)
#
# Output:
#   experiments/5.2-quart-vs-quit/results/results_<TIMESTAMP>.csv
#
# CSV columns:
#   workload, K, L, tree_type, avg_insert_ns, avg_query_ns
#
#   tree_type values: QuART_stail, QuIT_2k
#   Speedup (Figure 9) = QuIT_2k time / QuART_stail time for each (K,L) cell.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD="$REPO_ROOT/build"
QUIT_BUILD="$SCRIPT_DIR/build"
JOBS="${JOBS:-$(nproc)}"

# Bootstrap: clone deps, generate workloads, build main binaries (idempotent)
source "$SCRIPT_DIR/../setup.sh"

# Build the local QuIT runner if not already built
if [[ ! -x "$QUIT_BUILD/run_quit_2k" ]]; then
    echo "[5.2] Building run_quit_2k..."
    mkdir -p "$QUIT_BUILD"
    cmake -S "$SCRIPT_DIR" -B "$QUIT_BUILD" -DCMAKE_BUILD_TYPE=Release -Wno-dev
    make -C "$QUIT_BUILD" -j"$JOBS"
fi

WORKLOAD_DIR="${WORKLOAD_DIR:-$WORKLOAD_DIR}"
REPEAT="${REPEAT:-1}"  # paper used 5; increase for publication-quality averages
N=500000000

SUFFIX=$(date +"%Y%m%d_%H%M%S")
RESULTS_DIR="$SCRIPT_DIR/results"
RESULTS_FILE="$RESULTS_DIR/results_${SUFFIX}.csv"
LOG_DIR="$RESULTS_DIR/logs_${SUFFIX}"

mkdir -p "$RESULTS_DIR" "$LOG_DIR"

echo "Experiment 5.2 — QuART vs QuIT"
echo "  WORKLOAD_DIR : $WORKLOAD_DIR"
echo "  REPEAT       : $REPEAT"
echo "  Results      : $RESULTS_FILE"
echo "  Logs         : $LOG_DIR"
echo ""

# ---------------------------------------------------------------------------
# CSV header
# ---------------------------------------------------------------------------
echo "workload,K,L,tree_type,avg_insert_ns,avg_query_ns" > "$RESULTS_FILE"

TREES=(QuART_stail QuIT)

# ---------------------------------------------------------------------------
# Helper: run one (workload, tree) configuration REPEAT times and average
# ---------------------------------------------------------------------------
run_config() {
    local FILE="$1" N_VAL="$2" K_VAL="$3" L_VAL="$4" TREE="$5"
    local WNAME LOG INSERT_SUM QUERY_SUM FAILED BINARY
    WNAME="$(basename "$FILE" .bin)"
    LOG="$LOG_DIR/${WNAME}.log"
    INSERT_SUM=0
    QUERY_SUM=0
    FAILED=0

    # QuIT_2k uses its own binary; all QuART variants use the main run binary
    if [[ "$TREE" == QuIT* ]]; then
        BINARY="$QUIT_BUILD/run_quit_2k"
    else
        BINARY="$BUILD/run"
    fi

    echo "=== workload=$WNAME  tree=$TREE ===" >> "$LOG"

    for ((i=1; i<=REPEAT; i++)); do
        echo "--- Run $i/$REPEAT ---" >> "$LOG"
        OUTPUT=("$BINARY" -f "$FILE" -N "$N_VAL")
        [[ "$TREE" != QuIT* ]] && OUTPUT+=(-t "$TREE")
        OUTPUT=$("${OUTPUT[@]}" 2>>"$LOG") || true
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
# K=0, L=0 — fully sorted (bottom row of Figure 9)
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
