// test_kfp.cpp — benchmark QuART_kfp<3> vs ART on 3 real bods workload streams
//
// Reads 3 binary workload files (key_int_t little-endian), randomly interleaves
// them key-by-key, inserts into QuART_kfp<3> and plain ART, and reports timing.
// Key width is controlled by QUART_KEY_64: 32-bit by default, 64-bit if defined.
//
// Build: cmake --build build --target test_kfp
// Run  : ./build/test_kfp

#include <cassert>
#include <chrono>
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <random>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define QUART_KFP_STATS

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

static const char* WORKLOAD_FILES[3] = {
    "/scratch/cgokmen/bods/workloads/workload_N200000000_1_L1_start1.bin",
    "/scratch/cgokmen/bods/workloads/workload_N200000000_K1_L1_start400000001.bin",
    "/scratch/cgokmen/bods/workloads/workload_N200000000_K1_L1_start800000001.bin",
};

int main() {
    MappedFile files[3];
    for (int i = 0; i < 3; i++) {
        if (!files[i].open(WORKLOAD_FILES[i])) return 1;
        cout << "Loaded stream " << i << ": " << files[i].count << " keys\n";
    }

    // Use the minimum count so all streams are the same length.
    size_t N = files[0].count;
    for (int i = 1; i < 3; i++) N = min(N, files[i].count);
    cout << "Using " << N << " keys per stream (" << 3*N << " total)\n\n";

    uint8_t key[keyBytes];
    long long kfp_ns, art_ns;

    // Fraction of keys to pre-load (not timed) before the measured insertion run.
    static constexpr double PRELOAD_FRAC = 0.5;
    const size_t preload_total = static_cast<size_t>(3.0 * N * PRELOAD_FRAC);
    cout << "Pre-loading " << preload_total << " keys (" << (PRELOAD_FRAC*100) << "%) before timed run\n\n";

    // ── QuART_kfp<3> ─────────────────────────────────────────────────────────
    {
        QuART_kfp<3> tree;
        size_t pos[3] = {0, 0, 0};
        mt19937 rng(42);

        // Pre-load phase (not timed)
        for (size_t i = 0; i < preload_total; ) {
            int active[3], na = 0;
            for (int w = 0; w < 3; w++) if (pos[w] < N) active[na++] = w;
            if (!na) break;
            int w = active[uniform_int_distribution<int>(0, na - 1)(rng)];
            key_int_t k = files[w].data[pos[w]++];
            encodeKey(k, key);
            tree.insert(key, k);
            ++i;
        }

        // Timed phase
        const size_t timed_keys = 3 * N - preload_total;
        auto t0 = chrono::high_resolution_clock::now();
        while (true) {
            int active[3], na = 0;
            for (int w = 0; w < 3; w++) if (pos[w] < N) active[na++] = w;
            if (!na) break;
            int w = active[uniform_int_distribution<int>(0, na - 1)(rng)];
            key_int_t k = files[w].data[pos[w]++];
            encodeKey(k, key);
            tree.insert(key, k);
        }
        auto t1 = chrono::high_resolution_clock::now();
        kfp_ns = chrono::duration_cast<chrono::nanoseconds>(t1 - t0).count();

        // Spot-check a few keys from each stream.
        for (int w = 0; w < 3; w++) {
            for (size_t idx : {(size_t)0, N/2, N-1}) {
                key_int_t k = files[w].data[idx];
                encodeKey(k, key);
                ArtNode* leaf = tree.lookup(key);
                if (!leaf || !isLeaf(leaf) || getLeafValue(leaf) != k) {
                    cerr << "FAIL: QuART_kfp lookup stream=" << w
                         << " idx=" << idx << " k=" << k << "\n";
                    return 1;
                }
            }
        }
        cout << "QuART_kfp<3>: " << kfp_ns / 1'000'000 << " ms"
             << "  (" << fixed << setprecision(1)
             << (double)timed_keys / (kfp_ns / 1e9) / 1e6 << " M inserts/s, "
             << timed_keys << " timed keys)\n";
#ifdef QUART_KFP_STATS
        long long total = tree.getFpInsertCount() + tree.getBridgeCount() + tree.getNoMatchCount();
        cout << "  FP_INSERT=" << tree.getFpInsertCount()
             << "  BRIDGE="    << tree.getBridgeCount()
             << "  NO_MATCH="  << tree.getNoMatchCount()
             << "  (fp_insert%=" << fixed << setprecision(1)
             << 100.0 * tree.getFpInsertCount() / total << "%)\n";
#endif
    }

    // ── plain ART ────────────────────────────────────────────────────────────
    {
        ART::ART tree;
        size_t pos[3] = {0, 0, 0};
        mt19937 rng(42);  // same seed → identical sequence

        // Pre-load phase (not timed)
        for (size_t i = 0; i < preload_total; ) {
            int active[3], na = 0;
            for (int w = 0; w < 3; w++) if (pos[w] < N) active[na++] = w;
            if (!na) break;
            int w = active[uniform_int_distribution<int>(0, na - 1)(rng)];
            key_int_t k = files[w].data[pos[w]++];
            encodeKey(k, key);
            tree.insert(key, k);
            ++i;
        }

        // Timed phase
        const size_t timed_keys = 3 * N - preload_total;
        auto t0 = chrono::high_resolution_clock::now();
        while (true) {
            int active[3], na = 0;
            for (int w = 0; w < 3; w++) if (pos[w] < N) active[na++] = w;
            if (!na) break;
            int w = active[uniform_int_distribution<int>(0, na - 1)(rng)];
            key_int_t k = files[w].data[pos[w]++];
            encodeKey(k, key);
            tree.insert(key, k);
        }
        auto t1 = chrono::high_resolution_clock::now();
        art_ns = chrono::duration_cast<chrono::nanoseconds>(t1 - t0).count();

        cout << "ART:          " << art_ns / 1'000'000 << " ms"
             << "  (" << fixed << setprecision(1)
             << (double)timed_keys / (art_ns / 1e9) / 1e6 << " M inserts/s, "
             << timed_keys << " timed keys)\n";
    }

    cout << "\nSpeedup (QuART_kfp / ART): " << fixed << setprecision(2)
         << (double)art_ns / kfp_ns << "x\n";
    return 0;
}
