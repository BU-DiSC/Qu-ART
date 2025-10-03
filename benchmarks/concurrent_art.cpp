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
#include <barrier>

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

struct ThreadArgs {
    ART::ConcurrentART* tree;
    const std::vector<uint32_t>* keys;
    int thread_id;
    int num_threads;
    int start_idx;
    int end_idx;
    std::chrono::nanoseconds* execution_time;
};

void worker_thread(ThreadArgs* args) {
    auto start = std::chrono::high_resolution_clock::now();
    
    // Each thread processes its assigned range
    for (int i = args->start_idx; i < args->end_idx; i++) {
        uint8_t key4[4];
        ART::loadKey((*args->keys)[i], key4);
        args->tree->insert(key4, (*args->keys)[i]);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    args->execution_time[args->thread_id] = 
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
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
    ART::ConcurrentART* tree = new ART::ConcurrentART();
    
    // Pre-insert first key to avoid root race
    if (N > 0) {
        uint8_t key4[4];
        ART::loadKey(keys[0], key4);
        tree->insert(key4, keys[0]);
    }
    
    const int startIdx = (N > 0) ? 1 : 0;
    const int remaining = N - startIdx;
    
    // Prepare thread arguments
    std::vector<ThreadArgs> thread_args(threads);
    std::vector<std::thread> thread_pool;
    std::vector<std::chrono::nanoseconds> execution_times(threads);
    
    // Distribute work evenly among threads
    for (int t = 0; t < threads; t++) {
        int chunk_size = remaining / threads;
        int extra = remaining % threads;
        
        thread_args[t].tree = tree;
        thread_args[t].keys = &keys;
        thread_args[t].thread_id = t;
        thread_args[t].num_threads = threads;
        thread_args[t].start_idx = startIdx + t * chunk_size + std::min(t, extra);
        thread_args[t].end_idx = startIdx + (t + 1) * chunk_size + std::min(t + 1, extra);
        thread_args[t].execution_time = execution_times.data();
    }
    
    // Start all threads simultaneously
    auto global_start = std::chrono::high_resolution_clock::now();
    
    for (int t = 0; t < threads; t++) {
        thread_pool.emplace_back(worker_thread, &thread_args[t]);
    }
    
    // Wait for all threads to complete
    for (auto& th : thread_pool) {
        th.join();
    }
    
    auto global_end = std::chrono::high_resolution_clock::now();
    auto total_time = std::chrono::duration_cast<std::chrono::nanoseconds>(global_end - global_start);
    
    if (verbose) {
        std::cout << "Total insertion time: " << total_time.count() << " ns" << std::endl;
        std::cout << "Per-thread times:" << std::endl;
        for (int t = 0; t < threads; t++) {
            std::cout << "Thread " << t << ": " << execution_times[t].count() << " ns" << std::endl;
        }
        std::cout << "Throughput: " << (N * 1e9 / total_time.count()) << " ops/sec" << std::endl;
    }
    
    delete tree;
    return 0;
}