#!/bin/bash
SUFFIX=$(date +"%Y%m%d_%H%M%S")
RESULTSDIR="results"
RESULTS="${RESULTSDIR}/results_${SUFFIX}.txt"
LOGDIR="${RESULTSDIR}/logs"

mkdir -p "$LOGDIR"

echo "N,K,L,type_of_tree,insert_time,query_time" > "$RESULTS"

for FILE in ../bods/workloads/workload_N*_K*_L*.txt; do
    [ -f "$FILE" ] || continue

    BASENAME=$(basename "$FILE")
    N=$(echo "$BASENAME" | sed -n 's/.*_N\([0-9]*\)_K[0-9]*_L[0-9]*.txt/\1/p')
    K=$(echo "$BASENAME" | sed -n 's/.*_N[0-9]*_K\([0-9]*\)_L[0-9]*.txt/\1/p')
    L=$(echo "$BASENAME" | sed -n 's/.*_N[0-9]*_K[0-9]*_L\([0-9]*\).txt/\1/p')
    LOGFILE="${LOGDIR}/log_${BASENAME%.txt}_${SUFFIX}.txt"

    # Run QuART
    echo "Running quart on $FILE" >> "$LOGFILE"
    QUART_OUTPUT=$(./build/quart -f "$FILE" 2>>"$LOGFILE")
    echo "$QUART_OUTPUT" >> "$LOGFILE"
    CSV_LINE=$(echo "$QUART_OUTPUT" | tail -1)
    INSERT_TIME=$(echo "$CSV_LINE" | cut -d',' -f1 | xargs)
    QUERY_TIME=$(echo "$CSV_LINE" | cut -d',' -f2 | xargs)
    echo "$N,$K,$L,QuART,$INSERT_TIME,$QUERY_TIME" >> "$RESULTS"

    # Run ART
    echo "Running art on $FILE" >> "$LOGFILE"
    ART_OUTPUT=$(./build/art -f "$FILE" 2>>"$LOGFILE")
    echo "$ART_OUTPUT" >> "$LOGFILE"
    CSV_LINE=$(echo "$ART_OUTPUT" | tail -1)
    INSERT_TIME=$(echo "$CSV_LINE" | cut -d',' -f1 | xargs)
    QUERY_TIME=$(echo "$CSV_LINE" | cut -d',' -f2 | xargs)
    echo "$N,$K,$L,ART,$INSERT_TIME,$QUERY_TIME" >> "$RESULTS"
done