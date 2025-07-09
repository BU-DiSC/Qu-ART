# QuART

This repository contains an implementation of the Adaptive Radix Tree (ART) and several QuART (Quotient Adaptive Radix Tree) variants with different fast-path strategies.

---

## How to Build

1. Create a `build` directory and switch to it:
    ```shell
    mkdir build/
    cd build/
    ```
2. Compile using CMake:
    ```shell
    cmake ..
    make
    ```

---

## How to Run

Run the executables in `build` with the following options:

```shell
./main [-v] [-N <num_keys>] -f <input_file>
```
```shell
./profile_inserts [-N <num_keys>]
```
```shell
./profile_inserts_with_file [-N <num_keys>] -f <input_file>
```
```shell
./art [-N <num_keys>] -f <input_file>
```
```shell
./quart_tail [-N <num_keys>] -f <input_file>
```
```shell
./quart_xtail [-N <num_keys>] -f <input_file>
```
```shell
./quart_lil [-N <num_keys>] -f <input_file>
```

### Arguments

- `-f <input_file>`: Path to the binary file that contains keys 
- `-N <num_keys>`: Number of keys to insert and query (optional, default = 5,000,000)
- `-v`: Verbose mode (optional, default = false)

> **Note:**  
> `profile_inserts` is a benchmark that only inserts and queries sorted data (10 million entries by default). It does not use a workload file and is intended for profiling the performance of bulk sorted inserts and queries.

### Example

```shell
./main -N 1000000 -f ../bods/workloads/workload_N1000000_K90_L10.bin
```

---

## How to Run Experiments

To automate experiments and record results for all tree variants and workloads, use the provided shell script:

```shell
bash run_experiments.sh
```

**Note:**  
The script expects input files to be named in the form:  
`workload_N{N}_K{K}_L{L}.bin`  
and located in the `../bods/workloads/` directory (relative to the project root).

This script will:
- Run all tree variants (ART, QuART_tail, QuART_xtail, QuART_lil) on all workload files in `../bods/workloads/`
- Repeat each experiment 7 times and record the average insertion and query times
- Save results in the `results/` directory with a timestamped filename

### Script Output

- Results are written to a file like `results/results_YYYYMMDD_HHMMSS.txt`
- Each line contains:  
  `N,K,L,type_of_tree,avg_insert_time,avg_query_time`

---

## QuART Variants

- **QuART_tail**: Standard QuART with tail optimization.
- **QuART_xtail**: QuART with extended/intelligent tail handling.
- **QuART_lil**: QuART with "lil" (lightweight/inline leaf) optimization.
- **ART**: Baseline Adaptive Radix Tree.

You can run each variant by building and running the corresponding executable in `build/` (e.g., `./quart_tail`, `./quart_xtail`, `./quart_lil`, `./art`).

---

## Notes

- Input files should be binary files containing 32-bit unsigned integer keys.
- You can modify `run_experiments.sh` to change the number of repetitions, workload location, or which tree variants are tested.
- Results are saved in CSV format for easy analysis.


