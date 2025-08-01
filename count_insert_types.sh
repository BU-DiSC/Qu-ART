#!/bin/bash
SUFFIX=$(date +"%Y%m%d_%H%M%S")
RESULTSDIR="results"
RESULTS="${RESULTSDIR}/fp_inserts_${SUFFIX}.csv"
LOGDIR="${RESULTSDIR}/logs"

mkdir -p "$LOGDIR"

echo "N,K,L,tree_type,fast_path_inserts,root_inserts" > "$RESULTS"

for FILE in ../bods/workloads/workload_N*_K*_L*.bin; do
    [ -f "$FILE" ] || continue

    BASENAME=$(basename "$FILE")
    N=$(echo "$BASENAME" | sed -n 's/.*_N\([0-9]*\)_K[0-9]*_L[0-9]*.bin/\1/p')
    K=$(echo "$BASENAME" | sed -n 's/.*_N[0-9]*_K\([0-9]*\)_L[0-9]*.bin/\1/p')
    L=$(echo "$BASENAME" | sed -n 's/.*_N[0-9]*_K[0-9]*_L\([0-9]*\).bin/\1/p')
    LOGFILE="${LOGDIR}/log_fp_${BASENAME%.txt}_${SUFFIX}.txt"

    for TREE in ART QuART_tail QuART_lil QuART_stail; do
        echo "Running $TREE on $FILE" >> "$LOGFILE"
        OUTPUT=$(./build/count_fp_inserts -f "$FILE" -N "$N" -t "$TREE" 2>>"$LOGFILE")
        echo "$OUTPUT" >> "$LOGFILE"
        CSV_LINE=$(echo "$OUTPUT" | tail -1)
        FP_INSERTS=$(echo "$CSV_LINE" | cut -d',' -f1 | xargs)
        ROOT_INSERTS=$(echo "$CSV_LINE" | cut -d',' -f2 | xargs)
        echo "$N,$K,$L,$TREE,$FP_INSERTS,$ROOT_INSERTS" >> "$RESULTS"
    done
done