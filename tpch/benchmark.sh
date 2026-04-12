#!/usr/bin/env bash
# benchmark.sh — Run ART and QuART_stail 30 times each and report averages.

set -euo pipefail

RUNS=200
WORKLOAD="/scratch/cgokmen/bods/workloads/workload_N6000000_K9667_L01.bin"
N=6000000
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$(cd "$SCRIPT_DIR/../build" && pwd)"
RUN="$BUILD/run"
RUN_QUIT="$BUILD/run_quit_2k"
RESULTS_DIR="$SCRIPT_DIR/results"
TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
CSV_FILE="$RESULTS_DIR/benchmark_${TIMESTAMP}.csv"
LOG_DIR="$RESULTS_DIR/logs/benchmark_${TIMESTAMP}"

mkdir -p "$RESULTS_DIR" "$LOG_DIR"

# Parse K and L from workload filename (e.g. workload_N6000000_K9667_L01.bin)
WNAME="$(basename "$WORKLOAD" .bin)"
K="$(echo "$WNAME" | grep -oP '(?<=_K)\d+')"
L="$(echo "$WNAME" | grep -oP '(?<=_L)\d+')"

# Write CSV header
echo "tree_type,repeat,N,K,L,insertion_ns,query_ns" > "$CSV_FILE"

run_tree() {
  local tree="$1"
  local binary="${2:-$RUN}"
  local log="$LOG_DIR/${tree}.log"
  local total_ins=0
  local total_qry=0

  echo "=== $tree ($RUNS runs) ===" | tee "$log"
  for i in $(seq 1 "$RUNS"); do
    # Last line of output is "insertion_ns,query_ns"
    result=$("$binary" -t "$tree" -f "$WORKLOAD" -v -N "$N" | tail -n1)
    ins=$(echo "$result" | cut -d',' -f1)
    qry=$(echo "$result" | cut -d',' -f2)
    echo "  run $i: ins=${ins} ns  qry=${qry} ns" | tee -a "$log"
    total_ins=$(( total_ins + ins ))
    total_qry=$(( total_qry + qry ))
  done

  local avg_ins=$(( total_ins / RUNS ))
  local avg_qry=$(( total_qry / RUNS ))
  echo "  avg: ins=${avg_ins} ns  qry=${avg_qry} ns" | tee -a "$log"
  echo | tee -a "$log"
  echo "$tree,$RUNS,$N,$K,$L,$avg_ins,$avg_qry" >> "$CSV_FILE"
}

echo "CSV:  $CSV_FILE"
echo "Logs: $LOG_DIR"

run_tree "BPTree"              "$RUN_QUIT"
run_tree "QuIT"                "$RUN_QUIT"
run_tree "ART"
run_tree "QuART_stail"
run_tree "QuART_lil"
run_tree "QuART_tail"

echo "Done."
