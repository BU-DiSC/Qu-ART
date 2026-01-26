#!/bin/bash
# filepath: compare_bulkload.sh

# Configuration
RESULTSDIR="results"
RESULTS="${RESULTSDIR}/bulkload_comparison_$(date +"%Y%m%d_%H%M%S").csv"
LOGDIR="${RESULTSDIR}/bulkload_logs_$(date +"%Y%m%d_%H%M%S")"

mkdir -p "$LOGDIR"

# Write CSV header
echo "N,insertion_method,insertion_time_ns,query_time_ns" > "$RESULTS"

# Test values from 100M to 2B
# Adjust these based on available memory and input file
TEST_SIZES=(100000000 250000000 500000000 750000000 1000000000 1500000000 2000000000)

# Number of repetitions for averaging
REPEAT=5

# Input file - adjust path as needed
INPUT_FILE="/home/grad1/cgokmen/bods/workloads/workload_N500000000_K0_L0.bin"

echo "Starting bulk load comparison..."
echo "Results will be saved to: $RESULTS"
echo "Logs will be saved to: $LOGDIR"
echo ""

for N in "${TEST_SIZES[@]}"; do
    echo "Testing with N=$N keys..."
    
    # Test regular insertion
    echo "  Running regular ART insertion..."
    INSERT_SUM=0
    QUERY_SUM=0
    
    LOGFILE="${LOGDIR}/log_N${N}_regular.txt"
    echo "=== Regular insertion for N=$N ===" > "$LOGFILE"
    echo "" >> "$LOGFILE"
    
    for ((i=1; i<=REPEAT; i++)); do
        echo "--- Run $i/$REPEAT ---" >> "$LOGFILE"
        
        OUTPUT=$(./build/run -f "$INPUT_FILE" -N "$N" -t ART 2>>"$LOGFILE")
        echo "$OUTPUT" >> "$LOGFILE"
        echo "" >> "$LOGFILE"
        
        CSV_LINE=$(echo "$OUTPUT" | tail -1)
        INSERT_TIME=$(echo "$CSV_LINE" | cut -d',' -f1 | xargs)
        QUERY_TIME=$(echo "$CSV_LINE" | cut -d',' -f2 | xargs)
        
        INSERT_SUM=$((INSERT_SUM + INSERT_TIME))
        QUERY_SUM=$((QUERY_SUM + QUERY_TIME))
        
        echo "    Run $i: insert=${INSERT_TIME}ns, query=${QUERY_TIME}ns"
    done
    
    AVG_INSERT_TIME=$((INSERT_SUM / REPEAT))
    AVG_QUERY_TIME=$((QUERY_SUM / REPEAT))
    echo "$N,regular,$AVG_INSERT_TIME,$AVG_QUERY_TIME" >> "$RESULTS"
    echo "  Regular avg: insert=${AVG_INSERT_TIME}ns, query=${AVG_QUERY_TIME}ns"
    
    # Test bulk load insertion
    echo "  Running bulk load ART insertion..."
    INSERT_SUM=0
    QUERY_SUM=0
    
    LOGFILE="${LOGDIR}/log_N${N}_bulkload.txt"
    echo "=== Bulk load insertion for N=$N ===" > "$LOGFILE"
    echo "" >> "$LOGFILE"
    
    for ((i=1; i<=REPEAT; i++)); do
        echo "--- Run $i/$REPEAT ---" >> "$LOGFILE"
        
        OUTPUT=$(./build/run -f "$INPUT_FILE" -N "$N" -t ART --bulkload 2>>"$LOGFILE")
        echo "$OUTPUT" >> "$LOGFILE"
        echo "" >> "$LOGFILE"
        
        CSV_LINE=$(echo "$OUTPUT" | tail -1)
        INSERT_TIME=$(echo "$CSV_LINE" | cut -d',' -f1 | xargs)
        QUERY_TIME=$(echo "$CSV_LINE" | cut -d',' -f2 | xargs)
        
        INSERT_SUM=$((INSERT_SUM + INSERT_TIME))
        QUERY_SUM=$((QUERY_SUM + QUERY_TIME))
        
        echo "    Run $i: insert=${INSERT_TIME}ns, query=${QUERY_TIME}ns"
    done
    
    AVG_INSERT_TIME=$((INSERT_SUM / REPEAT))
    AVG_QUERY_TIME=$((QUERY_SUM / REPEAT))
    echo "$N,bulkload,$AVG_INSERT_TIME,$AVG_QUERY_TIME" >> "$RESULTS"
    echo "  Bulk load avg: insert=${AVG_INSERT_TIME}ns, query=${AVG_QUERY_TIME}ns"
    echo ""
done

echo "Comparison complete!"
echo "Results saved to: $RESULTS"

# Generate summary report
echo ""
echo "=== SUMMARY ==="
column -t -s',' "$RESULTS"