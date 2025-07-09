
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <vector>

#include "ART.h"
#include "ArtNode.h"
#include "Chain.h"
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
    bool verbose = false;  // optional argument
    int N = 5000000;       // optional argument
    string input_file;     // required argument
    // Parse arguments; make sure to increment i by 2 if you consume an argument
    for (int i = 1; i < argc;) {
        if (string(argv[i]) == "-v") {
            verbose = true;
            i++;
        } else if (string(argv[i]) == "-N") {
            N = atoi(argv[i + 1]);
            i += 2;
        }
    }

    // read data
    // auto keys = read_bin<uint64_t>(input_file.c_str());
    std::vector<uint64_t> keys(N, 0);
    for (int i = 0; i < N; i++) keys[i] = i;

    // Build tree

    ART::ART* tree = new ART::ART();
    long long insertion_time = 0;
    for (uint64_t i = 0; i < N; i++) {
        uint8_t key[8];
        ART::loadKey(keys[i], key);
        auto start = chrono::high_resolution_clock::now();
        tree->insert(key, keys[i]);
        auto stop = chrono::high_resolution_clock::now();
        auto duration =
            chrono::duration_cast<chrono::nanoseconds>(stop - start);
        insertion_time += duration.count();
    }

    if (verbose) {
        cout << "Insertion time: " << insertion_time << " ns" << endl;
    }

    // Query tree
    long long query_time = 0;

    // Range Queries
    for (uint64_t i = 4; i < N / 10; i++) {
        uint8_t key[8], key2[8];
        ART::loadKey(keys[i], key);
        ART::loadKey(std::min((uint64_t)N - 1, keys[i] + 133), key2);
        auto start = chrono::high_resolution_clock::now();
        ART::Chain* ch = tree->rangelookup(key, 8, key2, 8, 0, 8);

        auto stop = chrono::high_resolution_clock::now();
        auto duration =
            chrono::duration_cast<chrono::nanoseconds>(stop - start);
        query_time += duration.count();
        // while(!ch->isEmpty()) {
        //     uint8_t k[8];
        //     // ART::loadKey(ART::getLeafValue(ch->pop_front()->nodeptr()),
        //     k); std::cout << ART::getLeafValue(ch->pop_front()->nodeptr()) <<
        //     ", ";
        // }
        // std::cout << std::endl;
        // auto stop = chrono::high_resolution_clock::now();
        // auto duration =
        //     chrono::duration_cast<chrono::nanoseconds>(stop - start);
        // query_time += duration.count();
        // assert(ART::isLeaf(leaf) && ART::getLeafValue(leaf) == keys[i]);
    }

    // simply output the times in csv format
    cout << insertion_time << "," << query_time << endl;

    return 0;
}
