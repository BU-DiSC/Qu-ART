#!/bin/bash
# Run insertion/query experiments on the TPC-C workload across tree types and dataset sizes.
# Results are written as CSV to results/tpcc_results_<timestamp>.csv
# Run from the repo root: bash tpcc/run_tpcc_experiments.sh

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

SUFFIX=$(date +"%Y%m%d_%H%M%S")
RESULTSDIR="$SCRIPT_DIR/results"
RESULTS="${RESULTSDIR}/tpcc_results_${SUFFIX}.csv"
LOGDIR="${RESULTSDIR}/tpcc_logs_${SUFFIX}"

mkdir -p "$LOGDIR"

echo "N,tree_type,insertion_time_ns,query_time_ns" > "$RESULTS"

# Path to the generated TPC-C workload file
INPUT_FILE="$SCRIPT_DIR/benchmarksql_workdir/workload.txt"

if [ ! -f "$INPUT_FILE" ]; then
    echo "ERROR: workload file not found at $INPUT_FILE"
    echo "Run tpcc/gen_tpcc.py first to generate it."
    exit 1
fi

# Dataset sizes to test (must be <= number of rows in INPUT_FILE)
TEST_SIZES=(1000000 5000000 10000000 50000000 100000000)

# Tree types to benchmark
TREES=(ART QuART_tail QuART_lil QuART_stail QuART_lil_can QuART_stail_reset QuART_stail_reset_bidir)

# Number of repetitions per configuration
REPEAT=10

echo "Starting TPC-C experiments..."
echo "Results -> $RESULTS"
echo "Logs    -> $LOGDIR"
echo ""

for N in "${TEST_SIZES[@]}"; do
    for TREE in "${TREES[@]}"; do
        LOGFILE="${LOGDIR}/log_N${N}_${TREE}.txt"
        echo "=== N=$N  tree=$TREE ===" > "$LOGFILE"

        INSERT_SUM=0
        QUERY_SUM=0
        FAILED=0

        for ((i=1; i<=REPEAT; i++)); do
            echo "--- Run $i/$REPEAT ---" >> "$LOGFILE"
            OUTPUT=$(${REPO_ROOT}/build/run -f "$INPUT_FILE" -N "$N" -t "$TREE" 2>>"$LOGFILE")
            STATUS=$?
            echo "$OUTPUT" >> "$LOGFILE"

            if [ $STATUS -ne 0 ] || [ -z "$OUTPUT" ]; then
                echo "  [WARN] run failed for N=$N tree=$TREE run=$i" | tee -a "$LOGFILE"
                FAILED=1
                break
            fi

            CSV_LINE=$(echo "$OUTPUT" | tail -1)
            INSERT_TIME=$(echo "$CSV_LINE" | cut -d',' -f1 | xargs)
            QUERY_TIME=$(echo "$CSV_LINE" | cut -d',' -f2 | xargs)
            INSERT_SUM=$((INSERT_SUM + INSERT_TIME))
            QUERY_SUM=$((QUERY_SUM + QUERY_TIME))
            echo "    run $i: insert=${INSERT_TIME}ns  query=${QUERY_TIME}ns"
        done

        if [ $FAILED -eq 0 ]; then
            AVG_INSERT=$((INSERT_SUM / REPEAT))
            AVG_QUERY=$((QUERY_SUM / REPEAT))
            echo "$N,$TREE,$AVG_INSERT,$AVG_QUERY" >> "$RESULTS"
            echo "  avg: insert=${AVG_INSERT}ns  query=${AVG_QUERY}ns"
        else
            echo "$N,$TREE,ERROR,ERROR" >> "$RESULTS"
        fi
    done
done

echo ""
echo "Done. Results saved to $RESULTS"