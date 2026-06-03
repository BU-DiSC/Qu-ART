// test_kfp.cpp — benchmark QuART_kfp<3> (FIFO), QuART_kfp<3> (FREQ_FILTER),
//                 and plain ART on 3 real bods workload streams.
//
// Runs four workloads, each a triple of binary workload files (key_int_t
// little-endian): 3x K=L=0, 3x K=L=1, 3x K=L=25, and a mixed K=L=0/1/25.
// For each, randomly interleaves the 3 streams key-by-key, inserts into each
// tree in turn, and reports timing.
// Key width is controlled by QUART_KEY_64: 32-bit by default, 64-bit if defined.
//
// Build: cmake --build build --target test_kfp
// Run  : ./build/test_kfp

#include <array>
#include <cassert>
#include <chrono>
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <random>
#include <vector>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

// The k-fp classification counters are controlled by the CMake option
// QUART_KFP_STATS (OFF by default; configure with -DQUART_KFP_STATS=ON).  They
// add a per-insert increment to the QuART_kfp hot path, so leaving them off
// keeps the timing numbers clean.  When off, the stat-printing blocks below
// (all #ifdef QUART_KFP_STATS) compile out and only timing is reported.

#include "ART.h"
#include "ArtNode.h"
#include "Helper.h"
#include "trees/QuART_kfp.h"
#include "QuArtNodeBulkLoadMethods.cpp"

using namespace std;
using namespace ART;

static void encodeKey(key_int_t k, uint8_t out[keyBytes]) { loadKey(k, out); }

// Memory-maps a binary file of key_int_t values.  Returns a pointer and size.
struct MappedFile {
    const key_int_t* data = nullptr;
    size_t           count = 0;
    size_t           bytes = 0;
    int              fd = -1;

    bool open(const char* path) {
        fd = ::open(path, O_RDONLY);
        if (fd < 0) { perror(path); return false; }
        struct stat st;
        fstat(fd, &st);
        bytes = (size_t)st.st_size;
        count = bytes / sizeof(key_int_t);
        void* p = mmap(nullptr, bytes, PROT_READ, MAP_PRIVATE, fd, 0);
        if (p == MAP_FAILED) { perror("mmap"); ::close(fd); fd = -1; return false; }
        madvise(p, bytes, MADV_SEQUENTIAL);
        data = reinterpret_cast<const key_int_t*>(p);
        return true;
    }

    ~MappedFile() {
        if (data) munmap(const_cast<key_int_t*>(data), bytes);
        if (fd >= 0) ::close(fd);
    }
};

// Four workloads, each a triple of streams that are randomly interleaved:
//   1. three K=L=0  streams (perfectly sorted runs)
//   2. three K=L=1  streams
//   3. three K=L=25 streams (more disorder)
//   4. one each of K=L=0, K=L=1, K=L=25 (mixed sortedness)
// Each triple uses three distinct start offsets so the key ranges don't overlap.
struct Workload {
    const char* name;
    const char* files[3];
};

static const Workload WORKLOADS[4] = {
    {"3x K=L=0", {
        "/scratch/cgokmen/bods/workloads/workload_N200000000_K0_L0_start1.bin",
        "/scratch/cgokmen/bods/workloads/workload_N200000000_K0_L0_start400000001.bin",
        "/scratch/cgokmen/bods/workloads/workload_N200000000_K0_L0_start800000001.bin",
    }},
    {"3x K=L=1", {
        "/scratch/cgokmen/bods/workloads/workload_N200000000_K1_L1_start1.bin",
        "/scratch/cgokmen/bods/workloads/workload_N200000000_K1_L1_start400000001.bin",
        "/scratch/cgokmen/bods/workloads/workload_N200000000_K1_L1_start800000001.bin",
    }},
    {"3x K=L=25", {
        "/scratch/cgokmen/bods/workloads/workload_N200000000_K25_L25_start1.bin",
        "/scratch/cgokmen/bods/workloads/workload_N200000000_K25_L25_start400000001.bin",
        "/scratch/cgokmen/bods/workloads/workload_N200000000_K25_L25_start800000001.bin",
    }},
    {"mixed K=L=0/1/25", {
        "/scratch/cgokmen/bods/workloads/workload_N200000000_K0_L0_start1.bin",
        "/scratch/cgokmen/bods/workloads/workload_N200000000_K1_L1_start400000001.bin",
        "/scratch/cgokmen/bods/workloads/workload_N200000000_K25_L25_start800000001.bin",
    }},
};

// Interleaves the three streams key-by-key (mt19937 seeded 42 so every tree sees
// the identical sequence), pre-loads PRELOAD_FRAC of the keys untimed, then times
// ONLY the inserts.  Stream selection, RNG draws, and encodeKey are all done
// BEFORE the timed region, so the measured nanoseconds are pure insert() cost
// with no per-op timer overhead or key-encoding mixed in.  Spot-checks a few keys
// for correctness, prints the timing line, and returns the insertion nanoseconds.
template <typename TreeT>
static long long run_interleaved(TreeT& tree, const MappedFile* files, size_t N,
                                 size_t preload_total, const char* label) {
    size_t pos[3] = {0, 0, 0};
    mt19937 rng(42);
    uint8_t key[keyBytes];

    // Pre-load phase (not timed).
    for (size_t i = 0; i < preload_total;) {
        int active[3], na = 0;
        for (int w = 0; w < 3; w++) if (pos[w] < N) active[na++] = w;
        if (!na) break;
        int w = active[uniform_int_distribution<int>(0, na - 1)(rng)];
        key_int_t k = files[w].data[pos[w]++];
        encodeKey(k, key);
        tree.insert(key, k);
        ++i;
    }

    // Materialise + pre-encode the remaining interleaved sequence BEFORE timing,
    // so the timed region holds ONLY tree.insert().  (The RNG continues from the
    // pre-load draws, so the sequence is still the seed-42 interleaving.)
    vector<key_int_t> vals;
    vector<array<uint8_t, keyBytes>> enc;
    while (true) {
        int active[3], na = 0;
        for (int w = 0; w < 3; w++) if (pos[w] < N) active[na++] = w;
        if (!na) break;
        int w = active[uniform_int_distribution<int>(0, na - 1)(rng)];
        key_int_t k = files[w].data[pos[w]++];
        vals.push_back(k);
        enc.emplace_back();
        encodeKey(k, enc.back().data());
    }
    const size_t timed_keys = vals.size();

    // Timed phase — insert() only, under a single whole-loop clock pair.
    auto t0 = chrono::high_resolution_clock::now();
    for (size_t i = 0; i < timed_keys; i++) tree.insert(enc[i].data(), vals[i]);
    auto t1 = chrono::high_resolution_clock::now();
    long long ns = chrono::duration_cast<chrono::nanoseconds>(t1 - t0).count();

    // Spot-check a few keys from each stream.
    for (int w = 0; w < 3; w++) {
        for (size_t idx : {(size_t)0, N / 2, N - 1}) {
            key_int_t k = files[w].data[idx];
            encodeKey(k, key);
            ArtNode* leaf = tree.lookup(key);
            if (!leaf || !isLeaf(leaf) || getLeafValue(leaf) != k) {
                cerr << "FAIL: " << label << " lookup stream=" << w
                     << " idx=" << idx << " k=" << k << "\n";
                exit(1);
            }
        }
    }

    cout << label << ": " << ns / 1'000'000 << " ms"
         << "  (" << fixed << setprecision(1)
         << (double)timed_keys / (ns / 1e9) / 1e6 << " M inserts/s, " << timed_keys
         << " timed keys)\n";
    return ns;
}

// Runs the full tree comparison suite on one workload (a triple of streams).
// Opens the three files, picks a common length, pre-loads PRELOAD_FRAC untimed,
// then times each enabled tree variant and prints the speedups.
static int run_workload(const Workload& wl, size_t key_limit,
                        bool run_ff, bool run_art) {
    cout << "════════════════════════════════════════════════════════════\n";
    cout << "Workload: " << wl.name << "\n";
    cout << "════════════════════════════════════════════════════════════\n";

    MappedFile files[3];
    for (int i = 0; i < 3; i++) {
        if (!files[i].open(wl.files[i])) return 1;
        cout << "Loaded stream " << i << ": " << files[i].count << " keys\n";
    }

    // Use the minimum count so all streams are the same length.
    size_t N = files[0].count;
    for (int i = 1; i < 3; i++) N = min(N, files[i].count);
    if (key_limit > 0 && key_limit < N) N = key_limit;
    cout << "Using " << N << " keys per stream (" << 3*N << " total)\n\n";

    long long ff_ns = 0, art_ns = 0;

    // Fraction of keys to pre-load (not timed) before the measured insertion run.
    static constexpr double PRELOAD_FRAC = 0.5;
    const size_t preload_total = static_cast<size_t>(3.0 * N * PRELOAD_FRAC);
    cout << "Pre-loading " << preload_total << " keys (" << (PRELOAD_FRAC*100) << "%) before timed run\n\n";

    // ── QuART_kfp<3, FREQ_FILTER> ──────────────────────────────────────────
    if (run_ff) {
        QuART_kfp<3, EvictionPolicy::FREQ_FILTER> tree;
        ff_ns = run_interleaved(tree, files, N, preload_total, "QuART_kfp<3,FF>");
#ifdef QUART_KFP_STATS
        long long total = tree.getFpInsertCount() + tree.getBridgeCount() + tree.getNoMatchCount();
        cout << "  FP_INSERT=" << tree.getFpInsertCount()
             << "  BRIDGE="    << tree.getBridgeCount()
             << "  NO_MATCH="  << tree.getNoMatchCount()
             << "  NO_MATCH_untracked=" << tree.getNoMatchUntrackedCount()
             << "  (fp_insert%=" << fixed << setprecision(1)
             << 100.0 * tree.getFpInsertCount() / total << "%)\n";
        long long hot = tree.getFpType4Count() + tree.getFpType16Count()
                      + tree.getFpType48Count() + tree.getFpType256Count();
        cout << "  hot-path fp types: Node4=" << tree.getFpType4Count()
             << " Node16=" << tree.getFpType16Count()
             << " Node48=" << tree.getFpType48Count()
             << " Node256=" << tree.getFpType256Count()
             << "  (Node256%=" << fixed << setprecision(1)
             << (hot ? 100.0 * tree.getFpType256Count() / hot : 0.0) << "%)\n"
             << "  fp_not_last_byte=" << tree.getFpNotLastByteCount() << "\n";
#endif
    }

    // ── plain ART ────────────────────────────────────────────────────────────
    if (run_art) {
        ART::ART tree;
        art_ns = run_interleaved(tree, files, N, preload_total, "ART");
    }

    if (run_art && art_ns > 0 && ff_ns > 0) {
        cout << "\nSpeedup vs ART:  FREQ_FILTER=" << fixed << setprecision(2)
             << (double)art_ns / ff_ns << "x\n";
    }
    cout << "\n";
    return 0;
}

int main(int argc, char** argv) {
    // Optional args:
    //   argv[1]: mode = "ff" | "art" | "both"
    //            ff runs only QuART_kfp<3,FREQ_FILTER> (for profiling);
    //            art runs only plain ART; both runs both.  (default "both")
    //   argv[2]: max keys per stream, 0 = all    (default 0)
    const char* mode = (argc > 1) ? argv[1] : "both";
    const bool run_ff  = (strcmp(mode, "ff")  == 0 || strcmp(mode, "both") == 0);
    const bool run_art = (strcmp(mode, "art") == 0 || strcmp(mode, "both") == 0);
    if (!run_ff && !run_art) {
        cerr << "Usage: test_kfp [ff|art|both] [max_keys_per_stream]\n";
        return 1;
    }
    const size_t key_limit = (argc > 2) ? (size_t)atoll(argv[2]) : 0;

    for (const Workload& wl : WORKLOADS) {
        int rc = run_workload(wl, key_limit, run_ff, run_art);
        if (rc != 0) return rc;
    }
    return 0;
}
