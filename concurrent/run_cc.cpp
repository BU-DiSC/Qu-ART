#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <barrier>
#include <cassert>

#include "../ART.h"
#include "../ArtNode.h"
#include "../Chain.h"
#include "../Helper.h"
#include "ConcurrentART.h"

using namespace std;

struct Config {
    bool verbose = false;
    int N = 500000000;
    string input_file;
    int num_threads = 1;
    int query_threads = 1;
    string results_csv = "results.csv";
};

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

namespace utils {
namespace executor {

template<typename Tree, typename KeyType>
class Workload {
private:
    Tree& tree;
    const Config& config;
    
public:
    Workload(Tree& t, const Config& conf) : tree(t), config(conf) {}
    
    struct WorkerStats {
        long long insertion_time = 0;
        long long query_time = 0;
        size_t keys_processed = 0;
        size_t queries_processed = 0;
    };
    
    // Insert operation for a single thread
    void insert_worker(const std::vector<KeyType>& keys, 
                      size_t start_idx, size_t end_idx,
                      std::barrier<>& sync_point,
                      WorkerStats& stats) {
        
        // Wait for all threads to be ready
        sync_point.arrive_and_wait();
        
        auto start_time = chrono::high_resolution_clock::now();
        
        for (size_t i = start_idx; i < end_idx; i++) {
            uint8_t key[4];
            ART::loadKey(keys[i], key);
            
            auto op_start = chrono::high_resolution_clock::now();
            tree.insertCC(key, keys[i]);
            auto op_stop = chrono::high_resolution_clock::now();
            
            auto duration = chrono::duration_cast<chrono::nanoseconds>(op_stop - op_start);
            stats.insertion_time += duration.count();
            stats.keys_processed++;
        }
        
        auto end_time = chrono::high_resolution_clock::now();
        if (config.verbose) {
            auto total_time = chrono::duration_cast<chrono::nanoseconds>(end_time - start_time);
            cout << "Thread processed " << stats.keys_processed 
                 << " insertions in " << total_time.count() << " ns" << endl;
        }
    }
    
    // Query operation for a single thread
    void query_worker(const std::vector<KeyType>& keys,
                     const std::vector<int>& random_indices,
                     size_t start_idx, size_t end_idx,
                     std::barrier<>& sync_point,
                     WorkerStats& stats) {
        
        // Wait for all threads to be ready
        sync_point.arrive_and_wait();
        
        auto start_time = chrono::high_resolution_clock::now();
        
        for (size_t i = start_idx; i < end_idx; i++) {
            int random = random_indices[i];
            uint8_t key[4];
            ART::loadKey(keys[random], key);
            
            auto op_start = chrono::high_resolution_clock::now();
            ART::ArtNode* leaf = tree.lookup(key);
            auto op_stop = chrono::high_resolution_clock::now();
            
            auto duration = chrono::duration_cast<chrono::nanoseconds>(op_stop - op_start);
            stats.query_time += duration.count();
            stats.queries_processed++;
            
            assert(ART::isLeaf(leaf) && ART::getLeafValue(leaf) == keys[random]);
        }
        
        auto end_time = chrono::high_resolution_clock::now();
        if (config.verbose) {
            auto total_time = chrono::duration_cast<chrono::nanoseconds>(end_time - start_time);
            cout << "Thread processed " << stats.queries_processed 
                 << " queries in " << total_time.count() << " ns" << endl;
        }
    }
    
    // Run insertion workload
    pair<long long, WorkerStats> run_insertions(const std::vector<KeyType>& keys) {
        WorkerStats total_stats;
        
        // HYBRID MODE: Always insert first 2 keys sequentially
        auto seq_start = chrono::high_resolution_clock::now();
        for (size_t i = 0; i < 2; i++) {
            uint8_t key[4];
            ART::loadKey(keys[i], key);
            
            auto op_start = chrono::high_resolution_clock::now();
            tree.insert(key, keys[i]); // we should use ART's insert here
            auto op_stop = chrono::high_resolution_clock::now();
            
            auto duration = chrono::duration_cast<chrono::nanoseconds>(op_stop - op_start);
            total_stats.insertion_time += duration.count();
            total_stats.keys_processed++;
        }
        auto seq_stop = chrono::high_resolution_clock::now();
        
        if (config.verbose) {
            auto seq_time = chrono::duration_cast<chrono::nanoseconds>(seq_stop - seq_start);
            cout << "Sequential phase: " << seq_time.count() << " ns" << endl;
        }
        
        // Enable concurrent mode and continue with remaining keys
        if (config.N > 2) {
            
            if (config.num_threads == 1) {
                // Single-threaded for remaining keys
                for (size_t i = 2; i < config.N; i++) {
                    uint8_t key[4];
                    ART::loadKey(keys[i], key);
                    
                    auto op_start = chrono::high_resolution_clock::now();
                    tree.insertCC(key, keys[i]);
                    tree.printTree();
                    auto op_stop = chrono::high_resolution_clock::now();
                    
                    auto duration = chrono::duration_cast<chrono::nanoseconds>(op_stop - op_start);
                    total_stats.insertion_time += duration.count();
                    total_stats.keys_processed++;
                }
            } else {
                // Multi-threaded for remaining keys
                vector<thread> threads;
                vector<WorkerStats> worker_stats(config.num_threads);
                barrier sync_point(config.num_threads);
                
                size_t remaining_keys = keys.size() - 2;
                size_t keys_per_thread = remaining_keys / config.num_threads;
                
                auto concurrent_start = chrono::high_resolution_clock::now();
                
                for (int t = 0; t < config.num_threads; t++) {
                    size_t start_idx = 2 + t * keys_per_thread;
                    size_t end_idx = (t == config.num_threads - 1) ? keys.size() : (2 + (t + 1) * keys_per_thread);
                    
                    threads.emplace_back(&Workload::insert_worker, this,
                                       ref(keys), start_idx, end_idx,
                                       ref(sync_point), ref(worker_stats[t]));
                }
                
                for (auto& t : threads) {
                    t.join();
                }
                
                auto concurrent_stop = chrono::high_resolution_clock::now();
                auto concurrent_wall_time = chrono::duration_cast<chrono::nanoseconds>(concurrent_stop - concurrent_start);
                
                // Aggregate worker stats
                for (const auto& stats : worker_stats) {
                    total_stats.keys_processed += stats.keys_processed;
                }
                
                total_stats.insertion_time += concurrent_wall_time.count();
                
                if (config.verbose) {
                    cout << "Concurrent phase wall time: " << concurrent_wall_time.count() << " ns" << endl;
                    
                    long long total_cpu_time = 0;
                    for (const auto& stats : worker_stats) {
                        total_cpu_time += stats.insertion_time;
                    }
                    cout << "Concurrent phase CPU time: " << total_cpu_time << " ns" << endl;
                }
            }
        }
        
        return {total_stats.insertion_time, total_stats};
    }
    
    // Run query workload
    pair<long long, WorkerStats> run_queries(const std::vector<KeyType>& keys) {
        WorkerStats total_stats;
        size_t num_queries = keys.size() / 100;
        
        // Pre-generate random indices
        srand(time(0));
        vector<int> random_indices(num_queries);
        for (size_t i = 0; i < num_queries; i++) {
            random_indices[i] = rand() % keys.size();
        }
        
        if (config.query_threads == 1) {
            // Single-threaded queries
            auto start_time = chrono::high_resolution_clock::now();
            
            for (size_t i = 0; i < num_queries; i++) {
                int random = random_indices[i];
                uint8_t key[4];
                ART::loadKey(keys[random], key);
                
                auto op_start = chrono::high_resolution_clock::now();
                ART::ArtNode* leaf = tree.lookup(key);
                auto op_stop = chrono::high_resolution_clock::now();
                
                auto duration = chrono::duration_cast<chrono::nanoseconds>(op_stop - op_start);
                total_stats.query_time += duration.count();
                total_stats.queries_processed++;
                
                assert(ART::isLeaf(leaf) && ART::getLeafValue(leaf) == keys[random]);
            }
            
            auto end_time = chrono::high_resolution_clock::now();
            auto wall_time = chrono::duration_cast<chrono::nanoseconds>(end_time - start_time);
            
            return {wall_time.count(), total_stats};
        } else {
            // Multi-threaded queries
            vector<thread> threads;
            vector<WorkerStats> worker_stats(config.query_threads);
            barrier sync_point(config.query_threads);
            
            size_t queries_per_thread = num_queries / config.query_threads;
            
            auto concurrent_start = chrono::high_resolution_clock::now();
            
            for (int t = 0; t < config.query_threads; t++) {
                size_t start_idx = t * queries_per_thread;
                size_t end_idx = (t == config.query_threads - 1) ? num_queries : (t + 1) * queries_per_thread;
                
                threads.emplace_back(&Workload::query_worker, this,
                                   ref(keys), ref(random_indices), 
                                   start_idx, end_idx,
                                   ref(sync_point), ref(worker_stats[t]));
            }
            
            for (auto& t : threads) {
                t.join();
            }
            
            auto concurrent_stop = chrono::high_resolution_clock::now();
            auto wall_time = chrono::duration_cast<chrono::nanoseconds>(concurrent_stop - concurrent_start);
            
            // Aggregate stats
            for (const auto& stats : worker_stats) {
                total_stats.queries_processed += stats.queries_processed;
            }
            
            if (config.verbose) {
                long long total_cpu_time = 0;
                for (const auto& stats : worker_stats) {
                    total_cpu_time += stats.query_time;
                }
                cout << "Query wall time: " << wall_time.count() << " ns" << endl;
                cout << "Query CPU time: " << total_cpu_time << " ns" << endl;
            }
            
            return {wall_time.count(), total_stats};
        }
    }
    
    void run_all(const std::vector<KeyType>& keys) {
        if (config.verbose) {
            cout << "Running workload with " << config.num_threads 
                 << " insertion threads, " << config.query_threads << " query threads" << endl;
        }
        
        // Run insertions
        auto [insertion_time, insert_stats] = run_insertions(keys);
        
        // Run queries
        auto [query_time, query_stats] = run_queries(keys);
        
        if (config.verbose) {
            cout << "Total insertion time: " << insertion_time << " ns" << endl;
            cout << "Total query time: " << query_time << " ns" << endl;
            
            double insertion_throughput = (double)insert_stats.keys_processed / (insertion_time / 1e9);
            double query_throughput = (double)query_stats.queries_processed / (query_time / 1e9);
            
            cout << "Insertion throughput: " << insertion_throughput << " ops/sec" << endl;
            cout << "Query throughput: " << query_throughput << " ops/sec" << endl;
        }
        
        // Output CSV format
        cout << config.num_threads << "," << config.query_threads << ","
             << insertion_time << "," << query_time << ","
             << insert_stats.keys_processed << "," << query_stats.queries_processed << endl;
    }
};

} // namespace executor
} // namespace utils

int main(int argc, char** argv) {
    Config config;

    string input_f;

    // Parse arguments
    for (int i = 1; i < argc;) {
        if (string(argv[i]) == "-v") {
            config.verbose = true;
            i++;
        } else if (string(argv[i]) == "-N") {
            config.N = atoi(argv[i + 1]);
            i += 2;
        } else if (string(argv[i]) == "-f") {
            config.input_file = argv[i + 1];
            input_f = argv[i + 1];
            i += 2;
        } else if (string(argv[i]) == "-it") {
            config.num_threads = atoi(argv[i + 1]);
            i += 2;
        } else if (string(argv[i]) == "-qt") {
            config.query_threads = atoi(argv[i + 1]);
            i += 2;
        } else {
            i++;
        }
    }

    // Load data
    auto keys = read_bin<uint32_t>(input_f.c_str());

    cout << "Succesfully loaded " << keys.size() << " keys from "
         << config.input_file << endl;

    if (config.verbose) {
        cout << "Loaded " << keys.size() << " keys" << endl;
        cout << "Configuration:" << endl;
        cout << "  Insertion threads: " << config.num_threads << endl;
        cout << "  Query threads: " << config.query_threads << endl;
    }
        
    ART::ConcurrentART tree;
    utils::executor::Workload<ART::ConcurrentART, uint32_t> workload(tree, config);
    workload.run_all(keys);

    return 0;
}