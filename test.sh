#!/bin/bash
# filepath: /projectnb/manoslab/cgokmen/Qu-ART/test.sh

SUFFIX=$(date +"%Y%m%d_%H%M%S")
RESULTSDIR="results"
mkdir -p "$RESULTSDIR"
RESULTS="${RESULTSDIR}/results_run_${SUFFIX}.txt"

echo "input_file,N,result" > "$RESULTS"

for FILE in ../bods/all_workloads/workload_N*_K*_L*.bin; do
    [ -f "$FILE" ] || continue

    BASENAME=$(basename "$FILE")
    N=$(echo "$BASENAME" | sed -n 's/.*_N\([0-9]*\)_K[0-9]*_L[0-9]*.bin/\1/p')

    OUTPUT=$(./build/run -t QuART_stail -f "$FILE" -N "$N" )
    # Write all output to results file, one block per workload
    echo "==== $FILE ====" >> "$RESULTS"
    echo "$OUTPUT" >> "$RESULTS"
    echo "" >> "$RESULTS"
done