// test_multi_fp_grid.cpp — 2-D sweep: (multi-fp slot count K) × (number of interleaved
// real bods workload streams), reporting QuART_multi_fp speedup over plain ART.
//
// For a chosen workload series (e.g. K0_L0 or K1_L1) and a stream count W,
// opens the first W generated N=20M workload files (offsets (i-1)*40M+1), reads
// up to keys_per_stream keys from each, randomly interleaves them key-by-key
// (seed 42 → identical sequence for every tree), pre-loads PRELOAD_FRAC
// untimed, then times the remaining inserts into plain ART and into
// QuART_multi_fp<K> for K in {1,2,4,8,16,32,64}, in both findSlot SearchModes
// (Branchless and SIMD).  Emits one CSV row per (mode,K) to stdout:
//
//   series,streams,keys_per_stream,mode,K,art_ns,multi_fp_ns,speedup
//
// (mode=art / K=0 is the plain-ART baseline row, so the CSV is self-contained.)
// Human-readable progress goes to stderr.  Loop W over {1,2,4,8,16,32,64} in
// the driver script (graphs/multi_fp_grid/run_grid.sh) to fill the full 7×7 grid.
//
// The point: with W concurrent sorted streams, a tree with K fp slots can only
// track K of them at once.  When K < W the slots thrash (evict / re-acquire),
// so each cell shows how much of the W-stream interleaving that slot budget
// captures — speedup should rise along K and plateau around K == W.
//
// Key width is controlled by QUART_KEY_64.  The bods files are 4-byte uint32,
// so this driver requires the default 32-bit build (asserted at startup).
//
// Build: cmake --build build --target test_multi_fp_grid
// Run  : ./build/test_multi_fp_grid <series> <num_streams> [keys_per_stream] [dir]
//        e.g. ./build/test_multi_fp_grid K0_L0 8 2000000   (keys_per_stream 0 =
//        full)

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "ART.h"
#include "ArtNode.h"
#include "Helper.h"
#include "QuArtNodeBulkLoadMethods.cpp"
#include "trees/QuART_multi_fp.h"

using namespace std;
using namespace ART;

// Fraction of keys pre-loaded (untimed) before the measured run, so the timed
// region reflects steady-state inserts rather than tree warm-up.
static constexpr double PRELOAD_FRAC = 0.5;

// Layout of the generated workloads (see ../bods/workloads).
static constexpr long long STREAM_N = 20000000;     // keys per file
static constexpr long long STREAM_STEP = 40000000;  // start-offset stride

static void encodeKey(key_int_t k, uint8_t out[keyBytes]) { loadKey(k, out); }

// Memory-maps a binary file of key_int_t (uint32) values.
struct MappedFile {
    const key_int_t* data = nullptr;
    size_t count = 0;
    size_t bytes = 0;
    int fd = -1;

    bool open(const char* path) {
        fd = ::open(path, O_RDONLY);
        if (fd < 0) {
            perror(path);
            return false;
        }
        struct stat st;
        fstat(fd, &st);
        bytes = (size_t)st.st_size;
        count = bytes / sizeof(key_int_t);
        void* p = mmap(nullptr, bytes, PROT_READ, MAP_PRIVATE, fd, 0);
        if (p == MAP_FAILED) {
            perror("mmap");
            ::close(fd);
            fd = -1;
            return false;
        }
        madvise(p, bytes, MADV_SEQUENTIAL);
        data = reinterpret_cast<const key_int_t*>(p);
        return true;
    }
    ~MappedFile() {
        if (data) munmap((void*)data, bytes);
        if (fd >= 0) ::close(fd);
    }
};

// Insert the pre-encoded interleaved sequence into a fresh tree, timing only
// the post-preload region, spot-check correctness, return the timed
// nanoseconds.
template <typename TreeT>
static long long run_one(TreeT& tree, const vector<key_int_t>& vals,
                         const vector<array<uint8_t, keyBytes>>& enc,
                         size_t preload, const char* label) {
    const size_t total = vals.size();

    for (size_t i = 0; i < preload; i++)
        tree.insert((uint8_t*)enc[i].data(), vals[i]);

    auto t0 = chrono::high_resolution_clock::now();
    for (size_t i = preload; i < total; i++)
        tree.insert((uint8_t*)enc[i].data(), vals[i]);
    auto t1 = chrono::high_resolution_clock::now();
    long long ns = chrono::duration_cast<chrono::nanoseconds>(t1 - t0).count();

    // Spot-check: every 1/8th of the full sequence must look up.
    uint8_t key[keyBytes];
    for (size_t f = 0; f < 8; f++) {
        size_t idx = (total / 8) * f;
        key_int_t k = vals[idx];
        encodeKey(k, key);
        ArtNode* leaf = tree.lookup(key);
        if (!leaf || !isLeaf(leaf) || getLeafValue(leaf) != k) {
            cerr << "FAIL: " << label << " lookup idx=" << idx << " k=" << k
                 << "\n";
            exit(1);
        }
    }

    const size_t timed = total - preload;
    cerr << left << setw(18) << label << right << ": " << setw(6)
         << ns / 1'000'000 << " ms  (" << fixed << setprecision(1) << setw(6)
         << (double)timed / (ns / 1e9) / 1e6 << " M inserts/s)";
    return ns;
}

// Emit one CSV row (machine-readable) on stdout.  A crashed run is recorded
// with multi_fp_ns = -1 and speedup = -1 so the cell is preserved (not lost) in the
// grid.  `mode` is the findSlot SearchMode ("branchless"/"simd"), or "art" for
// the baseline row.
static void emit_csv(const string& series, size_t streams, size_t kps,
                     const char* mode, int K, long long art_ns,
                     long long multi_fp_ns) {
    double speedup =
        (multi_fp_ns > 0 && art_ns > 0) ? (double)art_ns / multi_fp_ns : -1.0;
    printf("%s,%zu,%zu,%s,%d,%lld,%lld,%.4f\n", series.c_str(), streams, kps,
           mode, K, art_ns, multi_fp_ns, speedup);
    fflush(stdout);
}

// Run one tree benchmark in a forked child so a segfault (e.g. the known
// QuART_multi_fp slot-maintenance crash once num_active==K at high K) is captured as
// a failed cell rather than aborting the whole grid.  The child shares the
// read-only interleaved vals/enc via copy-on-write, builds the tree, runs the
// timed insert, writes the nanoseconds back over a pipe, and _exit()s (skipping
// the slow tree destructor).  Returns the timed ns, or -1 if the child crashed.
template <typename TreeT>
static long long run_one_isolated(const vector<key_int_t>& vals,
                                  const vector<array<uint8_t, keyBytes>>& enc,
                                  size_t preload, const char* label) {
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        perror("pipe");
        exit(1);
    }
    fflush(stdout);
    fflush(stderr);
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(1);
    }
    if (pid == 0) {
        close(pipefd[0]);
        TreeT tree;
        long long ns = run_one(tree, vals, enc, preload, label);
        ssize_t w = write(pipefd[1], &ns, sizeof(ns));
        (void)w;
        close(pipefd[1]);
        _exit(0);
    }
    close(pipefd[1]);
    long long ns = -1;
    ssize_t got = read(pipefd[0], &ns, sizeof(ns));
    close(pipefd[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    if (got != (ssize_t)sizeof(ns) || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0) {
        if (WIFSIGNALED(status))
            cerr << left << setw(18) << label << right << ": CRASHED (signal "
                 << WTERMSIG(status) << ")\n";
        return -1;
    }
    return ns;
}

// Run QuART_multi_fp<K> in one SearchMode, print stderr line + speedup, emit a CSV
// row tagged with the mode.
template <int K, SearchMode Search>
static void sweep_k_mode(const string& series, size_t streams, size_t kps,
                         const vector<key_int_t>& vals,
                         const vector<array<uint8_t, keyBytes>>& enc,
                         size_t preload, long long art_ns, const char* mode) {
    char label[40];
    snprintf(label, sizeof(label), "QuART_multi_fp<%d,%s>", K, mode);
    long long ns = run_one_isolated<QuART_multi_fp<K, EvictionPolicy::FIFO, Search>>(
        vals, enc, preload, label);
    if (ns > 0)
        cerr << "  speedup=" << fixed << setprecision(2) << (double)art_ns / ns
             << "x\n";
    emit_csv(series, streams, kps, mode, K, art_ns, ns);
}

// Sweep one K value across both findSlot SearchModes (Branchless vs SIMD) so the
// grid carries a direct A/B of the SoA+AVX2 classifier against the scalar
// branchless one.
template <int K>
static void sweep_k(const string& series, size_t streams, size_t kps,
                    const vector<key_int_t>& vals,
                    const vector<array<uint8_t, keyBytes>>& enc, size_t preload,
                    long long art_ns) {
    sweep_k_mode<K, SearchMode::Branchless>(series, streams, kps, vals, enc,
                                            preload, art_ns, "branchless");
    sweep_k_mode<K, SearchMode::SIMD>(series, streams, kps, vals, enc, preload,
                                      art_ns, "simd");
}

int main(int argc, char** argv) {
    if (sizeof(key_int_t) != 4) {
        cerr << "ERROR: test_multi_fp_grid requires 32-bit keys (the bods files are "
                "4-byte uint32).  Rebuild WITHOUT -DQUART_KEY_64.\n";
        return 1;
    }
    if (argc < 3) {
        cerr << "usage: " << argv[0]
             << " <series e.g. K0_L0> <num_streams> [keys_per_stream (0=full)] "
                "[workload_dir]\n";
        return 1;
    }
    const string series = argv[1];
    size_t num_streams = (size_t)atoll(argv[2]);
    if (num_streams < 1) num_streams = 1;
    const size_t kps_arg = (argc > 3) ? (size_t)atoll(argv[3]) : 0;
    const string dir = (argc > 4) ? argv[4] : "/scratch/cgokmen/bods/workloads";

    // ── Open the first W workload files of the series
    // ───────────────────────────
    vector<MappedFile> files(num_streams);
    size_t per_stream = (kps_arg == 0) ? (size_t)STREAM_N : kps_arg;
    for (size_t s = 0; s < num_streams; s++) {
        long long start = (long long)s * STREAM_STEP + 1;
        char path[512];
        snprintf(path, sizeof(path), "%s/workload_N%lld_%s_start%lld.bin",
                 dir.c_str(), STREAM_N, series.c_str(), start);
        if (!files[s].open(path)) {
            cerr << "Could not open stream file: " << path << "\n";
            return 1;
        }
        per_stream = min(per_stream, files[s].count);
    }

    const size_t total = num_streams * per_stream;
    cerr << "════════════════════════════════════════════════════════════\n";
    cerr << "multi-fp grid cell:  series=" << series << "  streams=" << num_streams
         << "  keys/stream=" << per_stream << "  total=" << total << " keys\n";
    cerr << "key width=" << (sizeof(key_int_t) * 8)
         << "-bit  preload=" << (PRELOAD_FRAC * 100) << "%\n";

    // ── Randomly interleave the W streams key-by-key (seed 42), pre-encoding
    //    before any timing so the timed loop is pure insert().
    //    ─────────────────
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
                if (pos[w] < per_stream) active.push_back(w);
            if (active.empty()) break;
            size_t w = active[uniform_int_distribution<size_t>(
                0, active.size() - 1)(rng)];
            key_int_t k = files[w].data[pos[w]++];
            vals.push_back(k);
            enc.emplace_back();
            encodeKey(k, enc.back().data());
        }
    }
    files.clear();  // unmap; the interleaved copy is all we need now

    const size_t preload = (size_t)(total * PRELOAD_FRAC);
    cerr << "Pre-loading " << preload << " keys before timed run\n\n";

    // ── Baseline: plain ART
    // ────────────────────────────────────────────────────
    long long art_ns = run_one_isolated<::ART::ART>(vals, enc, preload, "ART");
    cerr << "  (baseline)\n";
    // ART's CSV row uses K=0 / mode=art so the baseline is recoverable too.
    emit_csv(series, num_streams, per_stream, "art", 0, art_ns, art_ns);

    // ── Sweep K = 1, 2, 4, 8, 16, 32, 64
    // ───────────────────────────────────────
    sweep_k<1>(series, num_streams, per_stream, vals, enc, preload, art_ns);
    sweep_k<2>(series, num_streams, per_stream, vals, enc, preload, art_ns);
    sweep_k<4>(series, num_streams, per_stream, vals, enc, preload, art_ns);
    sweep_k<8>(series, num_streams, per_stream, vals, enc, preload, art_ns);
    sweep_k<16>(series, num_streams, per_stream, vals, enc, preload, art_ns);
    sweep_k<32>(series, num_streams, per_stream, vals, enc, preload, art_ns);
    sweep_k<64>(series, num_streams, per_stream, vals, enc, preload, art_ns);

    cerr << "\n";
    return 0;
}
