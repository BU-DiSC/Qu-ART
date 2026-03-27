#!/bin/bash
# Runs all bods workloads with ART, QuART_stail_reset_bidir, and QuIT.
# Queries all N inserted keys after insertion.
# Run from the repo root: bash run_experiments_quit.sh

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKLOAD_DIR="/home/grad1/cgokmen/bods/workloads"

SUFFIX=$(date +"%Y%m%d_%H%M%S")
RESULTSDIR="${SCRIPT_DIR}/results"
RESULTS="${RESULTSDIR}/results_quit_${SUFFIX}.csv"
LOGDIR="${RESULTSDIR}/logs_quit_${SUFFIX}"

mkdir -p "$LOGDIR"

echo "workload,tree_type,insertion_time_ns,query_time_ns" > "$RESULTS"

REPEAT=5

echo "Starting experiments..."
echo "Results -> $RESULTS"
echo "Logs    -> $LOGDIR"
echo ""

run_config() {
    local BINARY="$1"
    local ARGS="$2"
    local TREE="$3"
    local WORKLOAD_NAME="$4"
    local LOGFILE="${LOGDIR}/log_${WORKLOAD_NAME}_${TREE}.txt"

    echo "=== workload=$WORKLOAD_NAME  tree=$TREE ===" > "$LOGFILE"

    local INSERT_SUM=0
    local QUERY_SUM=0
    local FAILED=0

    for ((i=1; i<=REPEAT; i++)); do
        echo "--- Run $i/$REPEAT ---" >> "$LOGFILE"
        OUTPUT=$("${SCRIPT_DIR}/build/${BINARY}" $ARGS 2>>"$LOGFILE")
        STATUS=$?
        echo "$OUTPUT" >> "$LOGFILE"

        if [ $STATUS -ne 0 ] || [ -z "$OUTPUT" ]; then
            echo "  [WARN] run failed for workload=$WORKLOAD_NAME tree=$TREE run=$i" | tee -a "$LOGFILE"
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
        echo "$WORKLOAD_NAME,$TREE,$AVG_INSERT,$AVG_QUERY" >> "$RESULTS"
        echo "  avg: insert=${AVG_INSERT}ns  query=${AVG_QUERY}ns"
    else
        echo "$WORKLOAD_NAME,$TREE,ERROR,ERROR" >> "$RESULTS"
    fi
}

for WORKLOAD_FILE in "${WORKLOAD_DIR}"/*.bin; do
    WORKLOAD_NAME=$(basename "$WORKLOAD_FILE" .bin)
    N=$(echo "$WORKLOAD_NAME" | grep -oP '(?<=_N)\d+')

    echo ""
    echo ">>> Workload: $WORKLOAD_NAME  (N=$N)"

    run_config "run"             "-f $WORKLOAD_FILE -N $N -t ART"                     "ART"                     "$WORKLOAD_NAME"
    run_config "run"             "-f $WORKLOAD_FILE -N $N -t QuART_stail_reset_bidir" "QuART_stail_reset_bidir"  "$WORKLOAD_NAME"
    run_config "run_quit_simple" "-f $WORKLOAD_FILE -N $N"                             "BPTree_256"              "$WORKLOAD_NAME"
    run_config "run_quit"        "-f $WORKLOAD_FILE -N $N"                             "QuIT_256"                "$WORKLOAD_NAME"
    run_config "run_quit_512"    "-f $WORKLOAD_FILE -N $N"                             "QuIT_512"                "$WORKLOAD_NAME"
done

echo ""
echo "Done. Results saved to $RESULTS"
