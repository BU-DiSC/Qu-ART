// test_conc_olc_single.cpp — single-STREAM OLC A/B benchmark.
//
// Takes ONE (near-sorted) key stream and inserts it concurrently with W
// threads, each thread owning a contiguous partition of the stream, into two
// trees that differ ONLY in whether they keep a shared fast path:
//   • QuART_olc_single_art   : plain OLC ART (no fast path).
//   • QuART_olc_single_stail : OLC ART + one globally shared fast-path tail.
// Both use identical OLC descent machinery, so the delta measures exactly the
// value (or cost) of a single shared fast path for one stream under
// concurrency.  Every inserted key is verified by lookup after all joins.
//
// Source: a real BoDS N=20M workload file (--series, default K0_L0), or an
// arbitrary file (--file PATH).  Keys are deduplicated in first-seen order
// (near-sortedness preserved; exact repeats dropped so ART's leaf-split can't
// loop on identical keys).  Partitioned into W contiguous index chunks.
//
// CSV row:
//   series,W,total,serial_art_ns,serial_stail_ns,olc_art_ns,olc_stail_ns,
//   olcart_Minserts_s,olcstail_Minserts_s,olcart_speedup_vs_art,
//   olcstail_speedup_vs_art,olcstail_vs_olcart
//
// Build: cmake --build build --target test_conc_olc_single
// Run  : ./build/test_conc_olc_single 8 --series K5_L5 [keys]
// 32-bit keys only.

#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "ART.h"
#include "ArtNode.h"
#include "Helper.h"
#include "QuArtNodeBulkLoadMethods.cpp"
#include "trees/QuART_conc_olc_single.h"
#include "trees/QuART_stail.h"

using namespace std;
using namespace ART;

static void encodeKey(key_int_t k, uint8_t out[keyBytes]) { loadKey(k, out); }

static void pinToCore(int core) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(core, &set);
    pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
}

// Load a BoDS-format file (raw key_int_t array), truncate to `limit` keys
// (0 = all), and drop exact duplicates in first-seen order.
static vector<key_int_t> loadStream(const string& path, size_t limit) {
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        perror(path.c_str());
        exit(1);
    }
    struct stat st;
    fstat(fd, &st);
    size_t count = (size_t)st.st_size / sizeof(key_int_t);
    size_t n = (limit == 0) ? count : min(limit, count);
    void* p = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (p == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    madvise(p, st.st_size, MADV_SEQUENTIAL);
    const key_int_t* data = reinterpret_cast<const key_int_t*>(p);
    vector<key_int_t> out;
    out.reserve(n);
    unordered_set<key_int_t> seen;
    seen.reserve(n * 2);
    size_t dropped = 0;
    for (size_t j = 0; j < n; j++) {
        key_int_t k = data[j];
        if (k == 0) {  // reserved / artifact
            dropped++;
            continue;
        }
        if (seen.insert(k).second)
            out.push_back(k);
        else
            dropped++;
    }
    munmap(p, st.st_size);
    ::close(fd);
    cerr << "  stream: " << path << "  kept " << out.size() << " / " << n
         << " (dropped " << dropped << " dup/zero)\n";
    return out;
}

template <typename TreeT>
static long long runSerial(const vector<array<uint8_t, keyBytes>>& enc,
                           const vector<key_int_t>& vals) {
    TreeT tree;
    auto t0 = chrono::high_resolution_clock::now();
    for (size_t j = 0; j < vals.size(); j++)
        tree.insert((uint8_t*)enc[j].data(), vals[j]);
    auto t1 = chrono::high_resolution_clock::now();
    return chrono::duration_cast<chrono::nanoseconds>(t1 - t0).count();
}

// Parallel insert (W threads, one contiguous partition each), then verify every
// key by lookup.  Returns timed insert ns; sets misses_out.
template <typename TreeT>
static long long runConcurrent(size_t W, const vector<key_int_t>& stream,
                               const vector<array<uint8_t, keyBytes>>& enc,
                               int numCores, long long& misses_out) {
    TreeT tree(static_cast<int>(W));
    const size_t total = stream.size();
    const size_t chunk = total / W;

    std::atomic<int> ready{0};
    std::atomic<bool> go{false};
    vector<std::thread> threads;
    threads.reserve(W);
    for (size_t i = 0; i < W; i++) {
        const size_t lo = i * chunk;
        const size_t hi = (i == W - 1) ? total : (i + 1) * chunk;
        threads.emplace_back([&, i, lo, hi]() {
            pinToCore((int)(i % numCores));
            ready.fetch_add(1, std::memory_order_acq_rel);
            while (!go.load(std::memory_order_acquire)) {
            }
            for (size_t j = lo; j < hi; j++)
                tree.insert((int)i, (uint8_t*)enc[j].data(), stream[j]);
        });
    }
    while (ready.load(std::memory_order_acquire) < (int)W) {
    }
    auto t0 = chrono::high_resolution_clock::now();
    go.store(true, std::memory_order_release);
    for (auto& th : threads) th.join();
    auto t1 = chrono::high_resolution_clock::now();
    long long conc_ns =
        chrono::duration_cast<chrono::nanoseconds>(t1 - t0).count();

    std::atomic<long long> misses{0};
    {
        vector<std::thread> vt;
        for (size_t i = 0; i < W; i++) {
            const size_t lo = i * chunk;
            const size_t hi = (i == W - 1) ? total : (i + 1) * chunk;
            vt.emplace_back([&, i, lo, hi]() {
                pinToCore((int)(i % numCores));
                uint8_t key[keyBytes];
                long long lm = 0;
                for (size_t j = lo; j < hi; j++) {
                    encodeKey(stream[j], key);
                    ArtNode* leaf = tree.lookup(key);
                    if (!leaf || !isLeaf(leaf) || getLeafValue(leaf) != stream[j])
                        lm++;
                }
                misses.fetch_add(lm, std::memory_order_relaxed);
            });
        }
        for (auto& th : vt) th.join();
    }
    misses_out = misses.load();
    return conc_ns;
}

int main(int argc, char** argv) {
    if (sizeof(key_int_t) != 4) {
        cerr << "ERROR: test_conc_olc_single requires 32-bit keys.\n";
        return 1;
    }
    if (argc < 2) {
        cerr << "usage: " << argv[0]
             << " <threads> [keys] [--series K_L] [--dir D] [--file PATH]\n";
        return 1;
    }
    size_t W = (size_t)atoll(argv[1]);
    if (W < 1) W = 1;
    size_t keys = (argc > 2 && argv[2][0] != '-') ? (size_t)atoll(argv[2]) : 0;
    string series = "K0_L0";
    string dir = "/scratch/cgokmen/bods/workloads";
    string file;
    for (int i = 2; i < argc; i++) {
        string a = argv[i];
        if (a == "--series" && i + 1 < argc)
            series = argv[++i];
        else if (a == "--dir" && i + 1 < argc)
            dir = argv[++i];
        else if (a == "--file" && i + 1 < argc)
            file = argv[++i];
    }
    if (file.empty()) {
        char path[512];
        snprintf(path, sizeof(path), "%s/workload_N20000000_%s_start1.bin",
                 dir.c_str(), series.c_str());
        file = path;
    }

    int numCores = (int)std::thread::hardware_concurrency();
    if (numCores < 1) numCores = 1;

    cerr << "════════════════════════════════════════════════════════════\n";
    cerr << "conc-olc-single:  series=" << series << "  threads=" << W
         << "  keys=" << (keys ? to_string(keys) : string("all"))
         << "  cores=" << numCores << "\n";

    vector<key_int_t> stream = loadStream(file, keys);
    const size_t total = stream.size();
    if (total < W) {
        cerr << "ERROR: fewer keys than threads.\n";
        return 1;
    }

    vector<array<uint8_t, keyBytes>> enc(total);
    for (size_t j = 0; j < total; j++) encodeKey(stream[j], enc[j].data());

    long long serial_art_ns = runSerial<::ART::ART>(enc, stream);
    cerr << "  serial ART        : " << setw(7) << serial_art_ns / 1'000'000
         << " ms\n";
    long long serial_stail_ns = runSerial<QuART_stail>(enc, stream);
    cerr << "  serial QuART_stail: " << setw(7) << serial_stail_ns / 1'000'000
         << " ms\n";

    long long m1 = 0, m2 = 0;
    long long olc_art_ns =
        runConcurrent<QuART_olc_single_art>(W, stream, enc, numCores, m1);
    cerr << "  OLC ART  (" << W << " thr): " << setw(7)
         << olc_art_ns / 1'000'000 << " ms  misses=" << m1 << "\n";
    long long olc_stail_ns =
        runConcurrent<QuART_olc_single_stail>(W, stream, enc, numCores, m2);
    cerr << "  OLC stail(" << W << " thr): " << setw(7)
         << olc_stail_ns / 1'000'000 << " ms  misses=" << m2 << "\n";

    if (m1 != 0 || m2 != 0) {
        cerr << "  *** MISSES (art=" << m1 << " stail=" << m2
             << ") — insertion FAILED or tree corrupted ***\n";
        return 2;
    }

    double art_mips = (double)total / (olc_art_ns / 1e9) / 1e6;
    double stail_mips = (double)total / (olc_stail_ns / 1e9) / 1e6;
    cerr << "  OLC ART   : " << fixed << setprecision(1) << art_mips
         << " M/s  (" << setprecision(2)
         << (double)serial_art_ns / olc_art_ns << "x serial ART)\n";
    cerr << "  OLC stail : " << setprecision(1) << stail_mips << " M/s  ("
         << setprecision(2) << (double)serial_art_ns / olc_stail_ns
         << "x serial ART,  " << (double)olc_art_ns / olc_stail_ns
         << "x OLC ART)\n";
    cerr << "  ALL " << total << " keys verified present in both.\n";

    printf("%s,%zu,%zu,%lld,%lld,%lld,%lld,%.2f,%.2f,%.4f,%.4f,%.4f\n",
           series.c_str(), W, total, serial_art_ns, serial_stail_ns,
           olc_art_ns, olc_stail_ns, art_mips, stail_mips,
           (double)serial_art_ns / olc_art_ns,
           (double)serial_art_ns / olc_stail_ns,
           (double)olc_art_ns / olc_stail_ns);
    return 0;
}
