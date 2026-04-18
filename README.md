# Qu-ART

## Requirements

- **Linux x86-64**: the codebase uses x86 SSE/AVX-512 intrinsics (`emmintrin.h`, `immintrin.h`) and the POSIX `sys/time.h` header. macOS and non-x86 systems are not supported.
- CMake ≥ 3.10, a C++17-capable compiler (GCC or Clang), and `make`.

## Running Experiments

All experiments are self-contained and idempotent. Running any `run.sh` will automatically invoke `experiments/setup.sh` to clone dependencies, generate workloads, and build binaries if not already done.

### Setup

```shell
bash experiments/setup.sh
```

### Experiment 5.1 — Benefits of QuART

Reproduces Figures 7 and 8 (fast-path insert distributions and insertion speedup heatmaps over the K-L sortedness grid, N=500M).

```shell
bash experiments/5.1-quart-benefits/run.sh
```

Results: `experiments/5.1-quart-benefits/results/results_<TIMESTAMP>.csv`

Optional env vars:
- `WORKLOAD_DIR` – path to BoDS workload `.bin` files (default: `/scratch/cgokmen/bods/workloads`)
- `REPEAT` – repetitions per configuration (default: 1; use 5 for paper-quality averages)

### Experiment 5.2 — QuART vs. QuIT

Reproduces Figure 9 (stail insertion and lookup speedup over QuIT across the K-L grid, N=500M).

```shell
bash experiments/5.2-quart-vs-quit/run.sh
```

Results: `experiments/5.2-quart-vs-quit/results/results_<TIMESTAMP>.csv`

Optional env vars: `WORKLOAD_DIR`, `REPEAT` (default: 1; use 5 for paper numbers)

### Experiment 5.3 — TPC-H Workload

Reproduces Figure 10 (insertion and query throughput on TPC-H near-sorted workload, N=6M).

```shell
bash experiments/5.3-tpch/run.sh
```

Results: `experiments/5.3-tpch/results/results_<TIMESTAMP>.csv`

Optional env vars:
- `WORKLOAD_FILE` – path to the TPC-H `.bin` file (default: `/scratch/cgokmen/bods/workloads/workload_N6000000_K9667_L01.bin`)
- `REPEAT` – repetitions (default: 10; use 200 for paper numbers)

### Experiment 5.4 — Bulk Loading Performance

Reproduces Figure 11 (insertion time vs. number of elements, 100M–2B, fully sorted keys).

```shell
bash experiments/5.4-bulkload/run.sh
```

Results: `experiments/5.4-bulkload/results/results_<TIMESTAMP>.csv`

Optional env vars: `REPEAT` (default: 5)
