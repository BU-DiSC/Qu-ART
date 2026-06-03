// test_kfp_sweep.cpp — sweep the fast-path slot count K over 1,2,4,8,16,32,64.
//
// Builds M independent perfectly-sorted key streams (disjoint, non-overlapping
// upper-byte ranges — equivalent to K=L=0 bods workloads), randomly interleaves
// them key-by-key, and inserts the identical sequence into QuART_kfp<K> for each
// K in {1,2,4,8,16,32,64}, plus plain ART as a baseline.  Reports per-K timing
// and the speedup over ART.
//
// The point of the sweep: with M concurrent sorted streams, a tree with K fp
// slots can only track K of them at once.  When K < M the slots thrash (evict /
// re-acquire), so each K shows how many simultaneous streams that slot budget
// captures before performance plateaus (around K == M).
//
// Key width is controlled by QUART_KEY_64: 32-bit by default, 64-bit if defined.
//
// Build: cmake --build build --target test_kfp_sweep
// Run  : ./build/test_kfp_sweep [keys_per_stream] [num_streams]
//        defaults: keys_per_stream = 1000000, num_streams = 64

#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <random>
#include <vector>

#include "ART.h"
#include "ArtNode.h"
#include "Helper.h"
#include "trees/QuART_kfp.h"
#include "QuArtNodeBulkLoadMethods.cpp"

using namespace std;
using namespace ART;

static void encodeKey(key_int_t k, uint8_t out[keyBytes]) { loadKey(k, out); }

// Fraction of keys pre-loaded (untimed) before the measured insertion run, so
// the timed region reflects steady-state inserts rather than tree warm-up.
static constexpr double PRELOAD_FRAC = 0.5;

// The K values to sweep.  Each is a distinct QuART_kfp template instantiation.
// (Kept as explicit calls below since K is a compile-time template parameter.)

// Insert the pre-encoded interleaved sequence into a fresh tree, timing only the
// post-preload region, spot-check correctness, and return the timed nanoseconds.
template <typename TreeT>
static long long run_one(TreeT& tree, const vector<key_int_t>& vals,
                         vector<array<uint8_t, keyBytes>>& enc, size_t preload,
                         const char* label) {
    const size_t total = vals.size();

    // Pre-load phase (not timed).
    for (size_t i = 0; i < preload; i++) tree.insert(enc[i].data(), vals[i]);

    // Timed phase — insert() only, under a single whole-loop clock pair.
    auto t0 = chrono::high_resolution_clock::now();
    for (size_t i = preload; i < total; i++) tree.insert(enc[i].data(), vals[i]);
    auto t1 = chrono::high_resolution_clock::now();
    long long ns = chrono::duration_cast<chrono::nanoseconds>(t1 - t0).count();

    // Spot-check correctness: every 1/8th of the full sequence must look up.
    uint8_t key[keyBytes];
    for (size_t f = 0; f < 8; f++) {
        size_t idx = (total / 8) * f;
        key_int_t k = vals[idx];
        encodeKey(k, key);
        ArtNode* leaf = tree.lookup(key);
        if (!leaf || !isLeaf(leaf) || getLeafValue(leaf) != k) {
            cerr << "FAIL: " << label << " lookup idx=" << idx << " k=" << k << "\n";
            exit(1);
        }
    }

    const size_t timed = total - preload;
    cout << left << setw(18) << label << right << ": " << setw(6)
         << ns / 1'000'000 << " ms  (" << fixed << setprecision(1) << setw(5)
         << (double)timed / (ns / 1e9) / 1e6 << " M inserts/s)";
    return ns;
}

// Sweep one K value, printing its timing line and speedup over ART.
template <int K>
static void sweep_k(const vector<key_int_t>& vals,
                    vector<array<uint8_t, keyBytes>>& enc, size_t preload,
                    long long art_ns) {
    char label[32];
    snprintf(label, sizeof(label), "QuART_kfp<%d>", K);
    QuART_kfp<K, EvictionPolicy::FIFO> tree;
    long long ns = run_one(tree, vals, enc, preload, label);
    if (art_ns > 0)
        cout << "  speedup=" << fixed << setprecision(2) << (double)art_ns / ns
             << "x";
    cout << "\n";
}

int main(int argc, char** argv) {
    const size_t keys_per_stream =
        (argc > 1) ? (size_t)atoll(argv[1]) : 1'000'000;
    size_t num_streams = (argc > 2) ? (size_t)atoll(argv[2]) : 64;
    if (num_streams < 1) num_streams = 1;

    cout << "════════════════════════════════════════════════════════════\n";
    cout << "k-fp slot-count sweep\n";
    cout << "════════════════════════════════════════════════════════════\n";
    cout << "streams=" << num_streams << "  keys/stream=" << keys_per_stream
         << "  total=" << num_streams * keys_per_stream << " keys\n";
    cout << "key width=" << (sizeof(key_int_t) * 8) << "-bit  preload="
         << (PRELOAD_FRAC * 100) << "%\n\n";

    // ── Generate M disjoint, perfectly-sorted streams ──────────────────────────
    // Stream s occupies keys [base_s, base_s + N).  The stride keeps the ranges
    // disjoint and (since N >> 256) gives each stream distinct upper bytes, so the
    // findSlot classifier sees them as separate workloads.
    const key_int_t stride = (key_int_t)(keys_per_stream + 1000);
    vector<vector<key_int_t>> streams(num_streams);
    for (size_t s = 0; s < num_streams; s++) {
        key_int_t base = (key_int_t)s * stride + 1;
        streams[s].resize(keys_per_stream);
        for (size_t i = 0; i < keys_per_stream; i++)
            streams[s][i] = base + (key_int_t)i;
    }

    // ── Randomly interleave the streams key-by-key (seed 42 → identical
    //    sequence for every tree) and pre-encode before any timing. ─────────────
    const size_t total = num_streams * keys_per_stream;
    vector<key_int_t> vals;
    vector<array<uint8_t, keyBytes>> enc;
    vals.reserve(total);
    enc.reserve(total);
    {
        vector<size_t> pos(num_streams, 0);
        vector<size_t> active;
        active.reserve(num_streams);
        mt19937 rng(42);
        while (true) {
            active.clear();
            for (size_t w = 0; w < num_streams; w++)
                if (pos[w] < keys_per_stream) active.push_back(w);
            if (active.empty()) break;
            size_t w = active[uniform_int_distribution<size_t>(0, active.size() - 1)(rng)];
            key_int_t k = streams[w][pos[w]++];
            vals.push_back(k);
            enc.emplace_back();
            encodeKey(k, enc.back().data());
        }
    }
    streams.clear();
    streams.shrink_to_fit();

    const size_t preload = (size_t)(total * PRELOAD_FRAC);
    cout << "Pre-loading " << preload << " keys before timed run\n\n";

    // ── Baseline: plain ART ────────────────────────────────────────────────────
    long long art_ns;
    {
        ART::ART tree;
        art_ns = run_one(tree, vals, enc, preload, "ART");
        cout << "  (baseline)\n";
    }

    // ── Sweep K = 1, 2, 4, 8, 16, 32, 64 ───────────────────────────────────────
    sweep_k<1>(vals, enc, preload, art_ns);
    sweep_k<2>(vals, enc, preload, art_ns);
    sweep_k<4>(vals, enc, preload, art_ns);
    sweep_k<8>(vals, enc, preload, art_ns);
    sweep_k<16>(vals, enc, preload, art_ns);
    sweep_k<32>(vals, enc, preload, art_ns);
    sweep_k<64>(vals, enc, preload, art_ns);

    cout << "\n";
    return 0;
}
