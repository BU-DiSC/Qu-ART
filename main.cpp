
#include "ART.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <vector>
#include <algorithm>

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
        } else if (string(argv[i]) == "-f") {
            input_file = argv[i + 1];
            i += 2;
        }
    }


    // read data
    auto keys = read_bin<uint32_t>(input_file.c_str());
    //keys = {5,4,3,2,1};
    // Build tree

    ART::ART* tree = new ART::ART();
    long long insertion_time = 0;
    for (uint64_t i = 0; i < N; i++) {
        uint8_t key[8];
        ART::loadKey(keys[i], key);
        auto start = chrono::high_resolution_clock::now();
        tree->insert(key, 0, keys[i], 8);
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
    for (uint64_t i = 0; i < N; i++) {
        uint8_t key[8];
        ART::loadKey(keys[i], key);
        auto start = chrono::high_resolution_clock::now();
        ART::ArtNode* leaf = tree->lookup(key, 8, 0, 8);
        auto stop = chrono::high_resolution_clock::now();
        auto duration =
            chrono::duration_cast<chrono::nanoseconds>(stop - start);
        query_time += duration.count();
        assert(ART::isLeaf(leaf) && ART::getLeafValue(leaf) == keys[i]);
    }

    if (verbose) {
        cout << "Query time: " << query_time << " ns" << endl;
    }

    // simply output the times in csv format
    cout << insertion_time << "," << query_time << endl;

    return 0;
}
