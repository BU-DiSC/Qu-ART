#!/usr/bin/env bash
# Experiment 5.4: Bulk Loading Performance
#
# Compares standard repeated ART insertion against QuART grouped bulk loading
# for increasing numbers of sorted keys (100M to 2B).  Output corresponds to
# Figure 11 in the paper: insertion time vs. number of elements.
#
# The bulk-load path generates sequential keys 1..N synthetically (independent
# of the workload file) to guarantee a fully sorted input.  The regular-insert
# path reads the first N keys from the sorted workload file; sizes larger than
# the file are skipped automatically.
#
# Usage:
#   bash experiments/5.4-bulkload/run.sh
#
# Environment variables (all optional):
#   WORKLOAD_FILE  – sorted (K=0) .bin workload file
#                    (default: /scratch/cgokmen/bods/workloads/workload_N500000000_K0_L0.bin)
#   REPEAT         – number of timed repetitions per configuration (default: 5)
#
# Output:
#   experiments/5.4-bulkload/results/results_<TIMESTAMP>.csv
#
# CSV columns:
#   N, insertion_method, avg_insert_ns, avg_query_ns
#
#   insertion_method: "regular" or "bulkload"
#   Speedup (Figure 11) = regular_time / bulkload_time at each N.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD="$REPO_ROOT/build"

# Bootstrap: clone deps, generate workloads, build binaries (idempotent)
source "$SCRIPT_DIR/../setup.sh"

WORKLOAD_FILE="${WORKLOAD_FILE:-$WORKLOAD_DIR/workload_N500000000_K0_L0.bin}"
REPEAT="${REPEAT:-5}"

# Test sizes matching Figure 11 (100M to 2B)
TEST_SIZES=(100000000 250000000 500000000 750000000 1000000000 1500000000 2000000000)

SUFFIX=$(date +"%Y%m%d_%H%M%S")
RESULTS_DIR="$SCRIPT_DIR/results"
RESULTS_FILE="$RESULTS_DIR/results_${SUFFIX}.csv"
LOG_DIR="$RESULTS_DIR/logs_${SUFFIX}"

mkdir -p "$RESULTS_DIR" "$LOG_DIR"

# Determine number of keys available in the workload file
FILE_KEYS=$(( $(stat -c%s "$WORKLOAD_FILE") / 4 ))
echo "Experiment 5.4 — Bulk Loading Performance"
echo "  WORKLOAD_FILE : $WORKLOAD_FILE  (${FILE_KEYS} keys)"
echo "  REPEAT        : $REPEAT"
echo "  Results       : $RESULTS_FILE"
echo "  Logs          : $LOG_DIR"
echo ""

# ---------------------------------------------------------------------------
# CSV header
# ---------------------------------------------------------------------------
echo "N,insertion_method,avg_insert_ns,avg_query_ns" > "$RESULTS_FILE"

# ---------------------------------------------------------------------------
# Helper: run a configuration REPEAT times and append average to CSV
# ---------------------------------------------------------------------------
run_config() {
    local N_VAL="$1" METHOD="$2"
    local EXTRA_ARGS=""
    [[ "$METHOD" == "bulkload" ]] && EXTRA_ARGS="--bulkload"

    local LOG="$LOG_DIR/N${N_VAL}_${METHOD}.log"
    local INSERT_SUM=0 QUERY_SUM=0 FAILED=0

    echo "=== N=$N_VAL  method=$METHOD ===" > "$LOG"

    for ((i=1; i<=REPEAT; i++)); do
        echo "--- Run $i/$REPEAT ---" >> "$LOG"
        # shellcheck disable=SC2086
        OUTPUT=$("$BUILD/run" -f "$WORKLOAD_FILE" -N "$N_VAL" -t ART $EXTRA_ARGS 2>>"$LOG") || true
        STATUS=$?
        echo "$OUTPUT" >> "$LOG"

        if [[ $STATUS -ne 0 || -z "$OUTPUT" ]]; then
            echo "  [WARN] run failed: N=$N_VAL method=$METHOD run=$i" >&2
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
        echo "$N_VAL,$METHOD,$AVG_INS,$AVG_QRY" >> "$RESULTS_FILE"
        printf "  avg_insert=%dns  avg_query=%dns\n" "$AVG_INS" "$AVG_QRY"
    else
        echo "$N_VAL,$METHOD,ERROR,ERROR" >> "$RESULTS_FILE"
    fi
}

# ---------------------------------------------------------------------------
# Run for each test size
# ---------------------------------------------------------------------------
for N in "${TEST_SIZES[@]}"; do
    echo ">>> N=$N"

    # Regular insertion requires the file to have enough keys
    if (( N <= FILE_KEYS )); then
        echo "  [regular]"
        run_config "$N" "regular"
    else
        echo "  [regular] SKIP — workload file has only ${FILE_KEYS} keys (need $N)" >&2
        echo "$N,regular,SKIPPED,SKIPPED" >> "$RESULTS_FILE"
    fi

    # Bulk load always works: it generates sequential keys 1..N synthetically
    echo "  [bulkload]"
    run_config "$N" "bulkload"
done

echo ""
echo "Done. Results saved to $RESULTS_FILE"
