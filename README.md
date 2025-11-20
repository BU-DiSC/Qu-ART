# Qu-ART

This repository contains an implementation of the Adaptive Radix Tree (ART) and several Qu-ART (Quick Adaptive Radix Tree) variants with different fast-path strategies.

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
./run [-v] [-N <num_keys>] -f <input_file> -t <tree_type>
```


### Arguments

- `-f <input_file>`: Path to the binary file that contains keys 
- `-N <num_keys>`: Number of keys to insert and query (optional, default = 5,000,000)
- `-v`: Verbose mode (optional, default = false)
- `-t <tree_type>`: Type of tree to use (`ART`, `QuART_tail`, or `QuART_lil`)

### Example

```shell
./run -N 1000000 -f ../bods/workloads/workload_N1000000_K90_L10.bin
```

---

## Qu-ART Variants

- **ART**: Baseline Adaptive Radix Tree.
- **QuART_tail**: ART with tail optimization.
- **QuART_lil**: ART with lil optimization.
- **QuART_stail**: ART with stail optimization.
- **QuART_stail_reset**: ART with stail optimization, supports fp resets too.

You can run each variant by using the `run` executable in `build/` with the `-t` option to select the tree type. For example:

```shell
./run -f <input_file> -N <num_keys> -t ART
./run -f <input_file> -N <num_keys> -t QuART_tail
./run -f <input_file> -N <num_keys> -t QuART_lil
./run -f <input_file> -N <num_keys> -t QuART_stail
./run -f <input_file> -N <num_keys> -t QuART_stail_reset
```

Replace `<input_file>` and `<num_keys>` with your workload file and desired number of keys.

---

## Notes

- Input files should be binary files containing 32-bit unsigned integer keys.
- You can modify `run_experiments.sh` to change the number of repetitions, workload location, or which tree variants are tested.
