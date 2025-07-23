#!/bin/bash
SUFFIX=$(date +"%Y%m%d_%H%M%S")
RESULTSDIR="results"
RESULTS="${RESULTSDIR}/results_${SUFFIX}.txt"
LOGDIR="${RESULTSDIR}/logs"

mkdir -p "$LOGDIR"

echo "N,K,L,type_of_tree,avg_insert_time,avg_query_time" > "$RESULTS"

REPEAT=5

for FILE in ../bods/workloads/workload_N*_K*_L*.bin; do
    [ -f "$FILE" ] || continue

    BASENAME=$(basename "$FILE")
    N=$(echo "$BASENAME" | sed -n 's/.*_N\([0-9]*\)_K[0-9]*_L[0-9]*.bin/\1/p')
    K=$(echo "$BASENAME" | sed -n 's/.*_N[0-9]*_K\([0-9]*\)_L[0-9]*.bin/\1/p')
    L=$(echo "$BASENAME" | sed -n 's/.*_N[0-9]*_K[0-9]*_L\([0-9]*\).bin/\1/p')
    LOGFILE="${LOGDIR}/log_${BASENAME%.txt}_${SUFFIX}.txt"

    for TREE in ART QuART_tail QuART_xtail QuART_xtailRevised QuART_lil QuART_lilRevised QuART_iglil QuART_iglilRevised; do
        INSERT_SUM=0
        QUERY_SUM=0

        for ((i=1; i<=REPEAT; i++)); do
            echo "Running $TREE on $FILE (run $i/$REPEAT)" >> "$LOGFILE"
            OUTPUT=$(./build/$(echo $TREE | tr '[:upper:]' '[:lower:]') -f "$FILE" 2>>"$LOGFILE")
            echo "$OUTPUT" >> "$LOGFILE"
            CSV_LINE=$(echo "$OUTPUT" | tail -1)
            INSERT_TIME=$(echo "$CSV_LINE" | cut -d',' -f1 | xargs)
            QUERY_TIME=$(echo "$CSV_LINE" | cut -d',' -f2 | xargs)
            INSERT_SUM=$((INSERT_SUM + INSERT_TIME))
            QUERY_SUM=$((QUERY_SUM + QUERY_TIME))
        done

        AVG_INSERT_TIME=$((INSERT_SUM / REPEAT))
        AVG_QUERY_TIME=$((QUERY_SUM / REPEAT))
        echo "$N,$K,$L,$TREE,$AVG_INSERT_TIME,$AVG_QUERY_TIME" >> "$RESULTS"
    done
done