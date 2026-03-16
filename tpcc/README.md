# TPC-C Workload Generator

## What is the workload?

`workload.txt` is a sequence of **N unique consecutive integers** (0, 1, 2, ..., N−1),
one per line, emitted in an order that reflects real transactional insert patterns from a
TPC-C benchmark run.

The integers are assigned by deterministic rank (`o_id`, `o_w_id`, `o_d_id`), but
**output in timestamp order** (`o_entry_d`). Because multiple warehouses and districts
insert orders concurrently, the result is a stream that is nearly sorted but with small
natural interleaving — a realistic "almost sorted" insert workload.

## How it is generated

The script `gen_tpcc.py` automates the full pipeline:

1. **Clone & build** [BenchmarkSQL](https://github.com/pgsql-io/benchmarksql) (Maven).
2. **Initialize** a PostgreSQL database with enough warehouses to cover the target row
   count (`ceil(TARGET_ROWS / 30_000)` warehouses, since TPC-C loads ~30,000 orders per
   warehouse).
3. **Run `runDatabaseBuild.sh`** — loads all warehouse, district, customer, item, stock,
   order, and order-line data into PostgreSQL.
4. **Run `runBenchmark.sh`** — executes a short live TPC-C run to add additional order
   entries with realistic timestamps.
5. **Export via `COPY ... TO STDOUT`** — a SQL window function ranks all orders by
   `(o_id, o_w_id, o_d_id)` to assign keys 0..N−1, then emits them sorted by
   `o_entry_d` (insertion timestamp) to preserve the TPC-C interleaving.

### Key configuration parameters (`gen_tpcc.py`)

| Parameter | Default | Description |
|-----------|---------|-------------|
| `TARGET_ROWS` | 100,000,000 | Number of integers in the output file |
| `AUTO_WAREHOUSES` | True | Automatically compute warehouse count |
| `LOAD_WORKERS` | 4 | Parallel workers for the data load phase |
| `RUN_MINS` | 1 | Duration of the live benchmark run |

## Sortedness characteristics (example run, N = 100,000,000)

Evaluated using `bods/estimate_k_l_from_input.py` on a generation with
`TARGET_ROWS = 100_000_000` (3,334 warehouses):

| Metric | Value | Percentage of N |
|--------|-------|-----------------|
| **N** (total elements) | 100,000,000 | 100% |
| **K** (displaced elements) | 46,430 | **0.046%** |
| **L** (max displacement) | 21 positions | — |
| Fixed window | Yes (window = 1) | — |

- **K = 0.046%**: fewer than 1 in 2,000 elements are out of their sorted position.
- **L = 21**: no element is more than 21 positions away from where it would appear in a
  fully sorted sequence.

This makes the TPC-C workload an excellent benchmark for data structures optimized for
nearly-sorted insert sequences.
