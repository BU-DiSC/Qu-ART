/*
 * TPC-C Insert Performance Experiment — Adaptive Radix Tree vs QuART
 *
 * Reproduces the TPC-C insert experiment from:
 *   "The Adaptive Radix Tree: ARTful Indexing for Main-Memory Databases"
 *   Viktor Leis, Alfons Kemper, Thomas Neumann, ICDE 2013
 *   https://db.in.tum.de/~leis/papers/ART.pdf
 *
 * Key structure (ORDER-LINE primary key, packed into 32 bits):
 *   Bits 31-28 : w_id      (4 bits, warehouse, fixed at 1 for this experiment)
 *   Bits 27-24 : d_id      (4 bits, district 1–10)
 *   Bits 23-8  : o_id      (16 bits, order id per district, monotonically growing)
 *   Bits  7-0  : ol_number (8 bits, order-line number 1–15)
 *
 * After ART::loadKey (bswap32), the byte order seen by the tree is:
 *   key[0] = (w_id << 4) | d_id   — 10 distinct values for 10 districts
 *   key[1] = o_id high byte
 *   key[2] = o_id low byte
 *   key[3] = ol_number
 *
 * Warehouse count is fixed at 1 (controlled by TPCC_NUM_WAREHOUSES below, kept
 * as 1 to maximise key sortedness as requested).
 *
 * Insertion order: keys are generated in NewOrder transaction order — a random
 * district is chosen per transaction, and all order-lines for that order are
 * inserted consecutively. This produces realistic partially-sorted input.
 *
 * Usage:
 *   tpcc_art [-N <num_transactions>] [-t ART|QuART_stail_reset_2] [-v] [-r <repeats>]
 *
 * Output (CSV):
 *   tree_type,num_keys,total_insert_ns,avg_insert_ns_per_key
 */

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <vector>

// Pull in ART and QuART headers (one level up from this file).
// ArtNodeNewMethods.cpp contains method bodies for QuART node types; it is
// included here as a compilation unit (the same pattern used in run.cpp via
// QuART_lil.h → ArtNodeNewMethods.cpp).
#include "../ART.h"
#include "../ArtNode.h"
#include "../Helper.h"
#include "../ArtNodeNewMethods.cpp"
#include "../trees/QuART_stail_reset_2.h"

using namespace std;
using namespace chrono;

// ---------------------------------------------------------------------------
// TPC-C constants
// ---------------------------------------------------------------------------
static constexpr int TPCC_NUM_WAREHOUSES = 1;
static constexpr int TPCC_DISTRICTS_PER_WH = 10;
static constexpr int OL_CNT_MIN = 5;
static constexpr int OL_CNT_MAX = 15;

// Key layout (32-bit, after bswap32 bytes are sent to the tree):
//   bits 31-28 : w_id      (4 bits, always 0x1 for warehouse=1)
//   bits 27-24 : d_id      (4 bits, districts 1-10)
//   bits 23-4  : o_id      (20 bits, 0..1,048,575 per district)
//   bits  3-0  : ol_number (4 bits, 1-15)
//
// Shrinking ol_number from 8→4 bits and giving those 4 bits to o_id raises
// the per-district order capacity from 65,535 to 1,048,575, pushing the
// total key capacity from ~6.5 M to ~157 M (10 districts × 1,048,575 × avg 10).
static constexpr int O_ID_MAX = (1 << 20) - 1;  // 1,048,575

// ---------------------------------------------------------------------------
// Key encoding
// ---------------------------------------------------------------------------
// Returns the raw uint32_t before bswap (the value also stored in the leaf).
static inline uint32_t make_ol_key(int w_id, int d_id, int o_id, int ol_number) {
    return (static_cast<uint32_t>(w_id)    << 28)
         | (static_cast<uint32_t>(d_id)    << 24)
         | (static_cast<uint32_t>(o_id)    <<  4)
         | (static_cast<uint32_t>(ol_number) & 0xF);
}

// ---------------------------------------------------------------------------
// Key generation: simulate NewOrder transactions
// ---------------------------------------------------------------------------
struct OLKey {
    uint32_t key_int;   // raw integer (stored as leaf value)
    uint8_t  key[4];    // bytes fed to tree->insert()
};

// Workload modes:
//
//  random     — each transaction picks a uniformly random district (baseline,
//                matches the ART paper). Average sorted run: ~10 keys.
//
//  batch N    — run N consecutive transactions in the same district before
//                picking a new random district. Average sorted run: ~10*N keys.
//                Models a multi-threaded system where threads are district-
//                affine for a burst before rebalancing.
//
//  sequential — process all transactions for district 1, then district 2,
//                …, district 10. Maximises sorted run length (all of d=k
//                before d=k+1). The district transition is a clean ART-level
//                jump, which QuART handles with a single fp reset.
//
// QuART_stail_reset_2 benefits grow as run length increases: in random mode
// the fp is invalidated every ~10 keys; in sequential mode it is invalidated
// only 10 times for the entire workload.
enum class WorkloadMode { RANDOM, BATCH, SEQUENTIAL };

static vector<OLKey> generate_tpcc_keys(int num_transactions,
                                        WorkloadMode mode = WorkloadMode::RANDOM,
                                        int batch_size = 100,
                                        uint64_t seed = 42) {
    mt19937 rng(seed);
    uniform_int_distribution<int> dist_rng(1, TPCC_DISTRICTS_PER_WH);
    uniform_int_distribution<int> ol_cnt_rng(OL_CNT_MIN, OL_CNT_MAX);

    // Per-district order counter (single warehouse).
    vector<int> next_o_id(TPCC_DISTRICTS_PER_WH + 1, 1);  // index 1..10

    vector<OLKey> keys;
    keys.reserve(static_cast<size_t>(num_transactions) * OL_CNT_MAX);

    const int w_id = 1;

    if (mode == WorkloadMode::SEQUENTIAL) {
        // All transactions for d=1 first, then d=2, …, d=10.
        // Creates the longest possible sorted runs and exactly 9 fp resets.
        int txns_per_district = num_transactions / TPCC_DISTRICTS_PER_WH;
        for (int d_id = 1; d_id <= TPCC_DISTRICTS_PER_WH; d_id++) {
            for (int t = 0; t < txns_per_district; t++) {
                int o_id = next_o_id[d_id];
                if (o_id > O_ID_MAX) continue;
                next_o_id[d_id]++;
                int ol_cnt = ol_cnt_rng(rng);
                for (int ol = 1; ol <= ol_cnt; ol++) {
                    OLKey k;
                    k.key_int = make_ol_key(w_id, d_id, o_id, ol);
                    ART::loadKey(k.key_int, k.key);
                    keys.push_back(k);
                }
            }
        }
    } else {
        // RANDOM and BATCH share the same loop; BATCH just holds the district
        // fixed for `batch_size` consecutive transactions.
        int d_id = dist_rng(rng);   // current district
        int batch_remaining = batch_size;

        for (int txn = 0; txn < num_transactions; txn++) {
            if (mode == WorkloadMode::RANDOM) {
                d_id = dist_rng(rng);
            } else {  // BATCH
                if (--batch_remaining <= 0) {
                    d_id = dist_rng(rng);
                    batch_remaining = batch_size;
                }
            }

            int o_id = next_o_id[d_id];
            if (o_id > O_ID_MAX) continue;
            next_o_id[d_id]++;

            int ol_cnt = ol_cnt_rng(rng);
            for (int ol = 1; ol <= ol_cnt; ol++) {
                OLKey k;
                k.key_int = make_ol_key(w_id, d_id, o_id, ol);
                ART::loadKey(k.key_int, k.key);
                keys.push_back(k);
            }
        }
    }
    return keys;
}

// ---------------------------------------------------------------------------
// Benchmark helpers
// ---------------------------------------------------------------------------
template <typename Tree>
static long long run_inserts(Tree* tree, const vector<OLKey>& keys) {
    long long total_ns = 0;
    for (const auto& k : keys) {
        auto t0 = high_resolution_clock::now();
        tree->insert(const_cast<uint8_t*>(k.key), static_cast<uintptr_t>(k.key_int));
        auto t1 = high_resolution_clock::now();
        total_ns += duration_cast<nanoseconds>(t1 - t0).count();
    }
    return total_ns;
}

// Verify every inserted key is found with the correct value.
template <typename Tree>
static void verify(Tree* tree, const vector<OLKey>& keys) {
    for (const auto& k : keys) {
        ART::ArtNode* leaf = tree->lookup(const_cast<uint8_t*>(k.key));
        assert(ART::isLeaf(leaf));
        assert(ART::getLeafValue(leaf) == static_cast<uintptr_t>(k.key_int));
    }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    int          num_transactions = 500000;
    string       tree_type        = "QuART_stail_reset_2";
    bool         verbose          = false;
    int          repeats          = 3;
    WorkloadMode mode             = WorkloadMode::RANDOM;
    int          batch_size       = 100;

    for (int i = 1; i < argc; ) {
        if (string(argv[i]) == "-N") {
            num_transactions = atoi(argv[i + 1]);
            i += 2;
        } else if (string(argv[i]) == "-t") {
            tree_type = argv[i + 1];
            i += 2;
        } else if (string(argv[i]) == "-v") {
            verbose = true;
            i++;
        } else if (string(argv[i]) == "-r") {
            repeats = atoi(argv[i + 1]);
            i += 2;
        } else if (string(argv[i]) == "-w") {
            string m = argv[i + 1];
            if      (m == "random")     mode = WorkloadMode::RANDOM;
            else if (m == "batch")      mode = WorkloadMode::BATCH;
            else if (m == "sequential") mode = WorkloadMode::SEQUENTIAL;
            else { cerr << "Unknown workload: " << m << ". Use random|batch|sequential.\n"; return 1; }
            i += 2;
        } else if (string(argv[i]) == "-B") {
            batch_size = atoi(argv[i + 1]);
            i += 2;
        } else {
            i++;
        }
    }

    const string mode_name = (mode == WorkloadMode::RANDOM)     ? "random" :
                             (mode == WorkloadMode::BATCH)       ? "batch(" + to_string(batch_size) + ")" :
                                                                   "sequential";

    // -----------------------------------------------------------------------
    // Generate keys (outside timing loop — same set for every repeat)
    // -----------------------------------------------------------------------
    const vector<OLKey> keys = generate_tpcc_keys(num_transactions, mode, batch_size);
    const size_t num_keys    = keys.size();

    if (num_keys == 0) {
        cerr << "Error: no keys generated. Reduce -N or increase O_ID_MAX.\n";
        return 1;
    }

    if (verbose) {
        cerr << "TPC-C experiment\n"
             << "  Warehouses : " << TPCC_NUM_WAREHOUSES << "\n"
             << "  Districts  : " << TPCC_DISTRICTS_PER_WH << "\n"
             << "  Workload   : " << mode_name << "\n"
             << "  Transactions: " << num_transactions << "\n"
             << "  Keys generated: " << num_keys << "\n"
             << "  Tree type  : " << tree_type << "\n"
             << "  Repeats    : " << repeats << "\n";
    }

    // -----------------------------------------------------------------------
    // Run benchmark (repeat and take average)
    // -----------------------------------------------------------------------
    long long total_insert_ns = 0;

    for (int rep = 0; rep < repeats; rep++) {
        long long insert_ns = 0;

        if (tree_type == "ART") {
            ART::ART* tree = new ART::ART();
            insert_ns = run_inserts(tree, keys);
            if (verbose) verify(tree, keys);
            delete tree;
        } else if (tree_type == "QuART_stail_reset_2") {
            ART::QuART_stail_reset_2* tree = new ART::QuART_stail_reset_2();
            insert_ns = run_inserts(tree, keys);
            if (verbose) verify(tree, keys);
            delete tree;
        } else {
            cerr << "Unknown tree type: " << tree_type
                 << ". Use ART or QuART_stail_reset_2.\n";
            return 1;
        }

        if (verbose) {
            cerr << "  [rep " << rep + 1 << "] insert: " << insert_ns
                 << " ns  ("
                 << (double)insert_ns / (double)num_keys << " ns/key)\n";
        }
        total_insert_ns += insert_ns;
    }

    const long long avg_insert_ns = total_insert_ns / repeats;
    const double    avg_per_key   = (double)avg_insert_ns / (double)num_keys;

    // Output: tree_type,workload,num_keys,avg_insert_ns,avg_insert_ns_per_key
    cout << tree_type << ","
         << mode_name << ","
         << num_keys << ","
         << avg_insert_ns << ","
         << avg_per_key << "\n";

    return 0;
}
