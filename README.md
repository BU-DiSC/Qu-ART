# ART

## How to Build

1. Create `build` directory and switch directories
    ```shell
    mkdir build/
    cd build/
    ```
2. Compile using `CMAKE`
    ```shell
    cmake ..
    make
    ```

## How to Run

Run the executable in `build` with the following options:
```shell
./main [-v] [-N <num_keys>] -f <input_file>
```
### Understanding the Input

f: path to the binary file that contains keys

N: number of keys to insert and query (optional, default = 5000000)

v: verbose mode (optional, default = false)
