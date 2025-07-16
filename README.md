# QuART

This repository contains an implementation of the Adaptive Radix Tree (ART) and several QuART (Quick Adaptive Radix Tree) variants with different fast-path strategies.

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
./test_range_query [-v] [-N <num_keys>] -f <input_file>
```
```shell
./run [-v] [-N <num_keys>] -f <input_file> -t <tree_type>
```


### Arguments

- `-f <input_file>`: Path to the binary file that contains keys 
- `-N <num_keys>`: Number of keys to insert and query (optional, default = 5,000,000)
- `-v`: Verbose mode (optional, default = false)
- `-t <tree_type>`: Type of tree to use (`ART` or `QuART_tail`)

### Example

```shell
./main -N 1000000 -f ../bods/workloads/workload_N1000000_K90_L10.bin
```

---

## QuART Variants

- **ART**: Baseline Adaptive Radix Tree.
- **QuART_tail**: Standard QuART with tail optimization.

You can run each variant by using the `run` executable in `build/` with the `-t` option to select the tree type. For example:

```shell
./run -f <input_file> -N <num_keys> -t ART
./run -f <input_file> -N <num_keys> -t QuART_tail
```

Replace `<input_file>` and `<num_keys>` with your workload file and desired number of keys.

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
- Run all tree variants (ART, QuART_tail) on all workload files in `../bods/workloads/`
- Repeat each experiment multiple times and record the average insertion and query times
- Save results in the `results/` directory with a timestamped filename

### Script Output

- Results are written to a file like `results/results_YYYYMMDD_HHMMSS.txt`
- Each line contains:  
  `N,K,L,type_of_tree,avg_insert_time,avg_query_time`

---

## Notes

- Input files should be binary files containing 32-bit unsigned integer keys.
- You can modify `run_experiments.sh` to change the number of repetitions, workload location, or which tree variants are tested.
- Results are saved in CSV format for easy analysis.