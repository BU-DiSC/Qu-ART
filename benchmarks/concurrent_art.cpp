#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

#include "QuARTVariants/ConcurrentART.h"
#include "ArtNode.h"
#include "ArtNodeNewMethods.cpp"
#include "Helper.h"

using namespace std;

template <typename key_type>
std::vector<key_type> read_bin(const char* filename) {
    std::ifstream inputFile(filename, std::ios::binary);
    inputFile.seekg(0, std::ios::end);
    const std::streampos fileSize = inputFile.tellg();
    inputFile.seekg(0, std::ios::beg);
    std::vector<key_type> data(fileSize / sizeof(key_type));
    inputFile.read(reinterpret_cast<char*>(data.data()), fileSize);
    return data;
}

int main(int argc, char** argv) {
    int N = 50000000;       // optional argument
    string input_file;      // required argument
    int threads = max(1u, std::thread::hardware_concurrency()); // optional argument
    bool verbose = false;   // verbose output

    // Parse arguments (consistent with other benchmarks)
    for (int i = 1; i < argc;) {
        if (string(argv[i]) == "-N") {
            N = atoi(argv[i + 1]);
            i += 2;
        } else if (string(argv[i]) == "-f") {
            input_file = argv[i + 1];
            i += 2;
        } else if (string(argv[i]) == "-t") {
            threads = atoi(argv[i + 1]);
            i += 2;
        } else if (string(argv[i]) == "-v") {
            verbose = true;
            i++;
        } else {
            i++;
        }
    }

    // read data
    auto keys = read_bin<uint32_t>(input_file.c_str());
    if ((int)keys.size() < N) N = (int)keys.size();

    if (verbose) {
        cout << "Running concurrent ART insertion benchmark:" << endl;
        cout << "N=" << N << ", threads=" << threads << ", file=" << input_file << endl;
    }

    // Build tree
    ART::ConcurrentART* tree = new ART::ConcurrentART();

    // Pre-initialize root with one key to avoid root race condition
    if (N > 0) {
        uint8_t key4[4];
        ART::loadKey(keys[0], key4);
        tree->insert(key4, keys[0]);
    }

    // Parallel inserts for indices [1, N)
    const int startIdx = (N > 0) ? 1 : 0;
    long long insertion_time = 0;
    {
        auto t0 = chrono::high_resolution_clock::now();
        vector<thread> ts;
        ts.reserve(threads);
        for (int tid = 0; tid < threads; ++tid) {
            ts.emplace_back([&, tid] {
                for (int i = startIdx + tid; i < N; i += threads) {
                    uint8_t key4[4];
                    ART::loadKey(keys[i], key4);
                    tree->insert(key4, keys[i]);
                }
            });
        }
        for (auto& th : ts) th.join();
        auto t1 = chrono::high_resolution_clock::now();
        insertion_time = chrono::duration_cast<chrono::nanoseconds>(t1 - t0).count();
    }

    if (verbose) {
        cout << "Insertion time: " << insertion_time << " ns" << endl;
    }

    // CSV output (consistent with other benchmarks)
    cout << insertion_time << endl;
    return 0;
}