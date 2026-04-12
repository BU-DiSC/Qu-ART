#!/usr/bin/env bash
# experiments/setup.sh
#
# Bootstrap script for the Qu-ART artifact.
# Run once (or call from any experiment run.sh) to:
#   1. Clone and build BoDS workload generator (https://github.com/BU-DiSC/bods)
#   2. Generate all workload .bin files needed by the experiments
#   3. Clone the quick-insertion-tree dependency into experiments/5.2-quart-vs-quit/
#   4. Build the Qu-ART project (CMake)
#
# Already-done steps are skipped automatically (idempotent).
#
# Usage:
#   bash experiments/setup.sh          # run standalone
#   source experiments/setup.sh        # source from a run.sh (inherits WORKLOAD_DIR etc.)
#
# Environment variables (all optional):
#   BODS_DIR   – where to clone/find BoDS  (default: experiments/bods)
#   JOBS       – parallel make jobs        (default: nproc)

set -euo pipefail

EXPERIMENTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$EXPERIMENTS_DIR/.." && pwd)"
BUILD="$REPO_ROOT/build"
BODS_DIR="${BODS_DIR:-$EXPERIMENTS_DIR/bods}"
QUIT_DIR="$EXPERIMENTS_DIR/5.2-quart-vs-quit/quick-insertion-tree"
JOBS="${JOBS:-$(nproc)}"

echo "=== Qu-ART artifact setup ==="
echo "  REPO_ROOT : $REPO_ROOT"
echo "  BODS_DIR  : $BODS_DIR"
echo "  BUILD     : $BUILD"
echo ""

# ---------------------------------------------------------------------------
# Helper: returns 0 if every expected workload file exists in a given dir
# ---------------------------------------------------------------------------
_all_workloads_present() {
    local DIR="$1"
    [[ -f "$DIR/workload_N500000000_K0_L0.bin" ]] || return 1
    for K in 1 5 10 25 100; do
        for L in 1 5 10 25 100; do
            [[ -f "$DIR/workload_N500000000_K${K}_L${L}.bin" ]] || return 1
        done
    done
    [[ -f "$DIR/workload_N6000000_K9667_L01.bin" ]] || return 1
    return 0
}

# If WORKLOAD_DIR is not set, check whether the repo ships the workloads
# (e.g. workloads/ at the repo root) before falling back to bods/workloads.
if [[ -z "${WORKLOAD_DIR:-}" ]]; then
    if _all_workloads_present "$REPO_ROOT/workloads"; then
        WORKLOAD_DIR="$REPO_ROOT/workloads"
        echo "[setup] Workloads found in repo at $WORKLOAD_DIR — skipping BoDS and generation."
        export WORKLOAD_DIR
    fi
fi

# ---------------------------------------------------------------------------
# 1. Clone and build BoDS  (skipped when all workloads are already present)
# ---------------------------------------------------------------------------
if [[ -z "${WORKLOAD_DIR:-}" ]] || ! _all_workloads_present "${WORKLOAD_DIR:-__none__}"; then

    if [[ ! -d "$BODS_DIR" ]]; then
        echo "[setup] Cloning BoDS..."
        git clone https://github.com/BU-DiSC/bods.git "$BODS_DIR"
    else
        echo "[setup] BoDS already present at $BODS_DIR"
    fi

    BODS_BUILD="$BODS_DIR/build"
    if [[ ! -x "$BODS_BUILD/sortedness_data_generator" ]]; then
        echo "[setup] Building BoDS..."
        mkdir -p "$BODS_BUILD"
        cmake -S "$BODS_DIR" -B "$BODS_BUILD" -DCMAKE_BUILD_TYPE=Release -Wno-dev
        make -C "$BODS_BUILD" -j"$JOBS"
    else
        echo "[setup] BoDS already built"
    fi

    GENERATOR="$BODS_BUILD/sortedness_data_generator"

fi

# ---------------------------------------------------------------------------
# 2. Generate workloads  (skipped when all workloads are already present)
# ---------------------------------------------------------------------------
# !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
# WARNING: LARGE DISK USAGE
#   This step generates 26 binary workload files at N=500,000,000 keys each
#   (~1.9 GB per file) plus one TPC-H workload at N=6,000,000 keys (~23 MB).
#   Total disk space required: ~49 GB.
#   Workloads are written to: ${WORKLOAD_DIR:-$BODS_DIR/workloads}
#   To skip generation, set WORKLOAD_DIR to a directory containing
#   pre-generated .bin files before running this script.
# !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
if [[ -n "${WORKLOAD_DIR:-}" ]] && _all_workloads_present "$WORKLOAD_DIR"; then
    echo "[setup] All workloads already present in $WORKLOAD_DIR — skipping generation."
else
    if [[ -z "${WORKLOAD_DIR:-}" ]]; then
        WORKLOAD_DIR="$BODS_DIR/workloads"
    fi
    mkdir -p "$WORKLOAD_DIR"

# Helper: write a TOML and generate the .bin file if missing
generate_workload() {
    local N="$1" K="$2" L="$3"
    # Encode K and L for the filename the same way BoDS does:
    # integers are used as-is; treat as plain values
    local OUTFILE="$WORKLOAD_DIR/workload_N${N}_K${K}_L${L}.bin"
    [[ -f "$OUTFILE" ]] && return 0

    echo "[setup] Generating workload N=$N K=$K L=$L ..."
    local TOML
    TOML="$(mktemp --suffix=.toml)"
    cat > "$TOML" <<EOF
title = "Qu-ART experiment workload"

[global]
domain = ${N}

[[partition]]
start_index = 0
number_of_entries = ${N}
K = ${K}
L = ${L}
seed = 1234
alpha = 1
beta = 1
payload = 0
window_size = 1
output_file = "${OUTFILE}"
is_fixed = false
is_binary = true
reverse_order = true
EOF
    "$GENERATOR" --toml_file "$TOML"
    rm -f "$TOML"
}

# --- 5.1 / 5.2  K-L grid, N=500M ---
N=500000000
echo "[setup] Checking N=${N} workloads..."
generate_workload "$N" 0 0
for K in 1 5 10 25 100; do
    for L in 1 5 10 25 100; do
        generate_workload "$N" "$K" "$L"
    done
done

# --- 5.3  TPC-H-inspired workload ---
# K≈96.67 L≈0.1 over N=6M keys (per SWARE methodology).
# BoDS encodes these as K=9667, L=01 in the filename (×100 and ×10 resp.).
# The generator accepts floats for K and L.
TPCH_FILE="$WORKLOAD_DIR/workload_N6000000_K9667_L01.bin"
if [[ ! -f "$TPCH_FILE" ]]; then
    echo "[setup] Generating TPC-H workload (N=6M, K≈96.67, L≈0.1)..."
    TOML="$(mktemp --suffix=.toml)"
    cat > "$TOML" <<EOF
title = "Qu-ART TPC-H workload"

[global]
domain = 6000000

[[partition]]
start_index = 0
number_of_entries = 6000000
K = 96.67
L = 0.1
seed = 1234
alpha = 1
beta = 1
payload = 0
window_size = 1
output_file = "${TPCH_FILE}"
is_fixed = false
is_binary = true
reverse_order = true
EOF
    "$GENERATOR" --toml_file "$TOML"
    rm -f "$TOML"
else
    echo "[setup] TPC-H workload already present"
fi

fi  # end: workloads not already present

# Export so run.sh scripts pick it up without extra flags
export WORKLOAD_DIR

# ---------------------------------------------------------------------------
# 3. Clone quick-insertion-tree
# ---------------------------------------------------------------------------
if [[ ! -d "$QUIT_DIR" ]]; then
    echo "[setup] Cloning quick-insertion-tree..."
    git clone https://github.com/BU-DiSC/quick-insertion-tree.git "$QUIT_DIR"
else
    echo "[setup] quick-insertion-tree already present"
fi

# ---------------------------------------------------------------------------
# 4. Build Qu-ART
# ---------------------------------------------------------------------------
mkdir -p "$BUILD"
if [[ ! -x "$BUILD/run" ]]; then
    echo "[setup] Building Qu-ART..."
    cmake -S "$REPO_ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release
    make -C "$BUILD" -j"$JOBS"
else
    echo "[setup] Qu-ART binaries already built"
fi

echo ""
echo "[setup] Done. WORKLOAD_DIR=$WORKLOAD_DIR"
