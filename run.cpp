#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <vector>

#include "ART.h"
#include "ArtNode.h"
// #include "ArtNodeNewMethods.cpp"
#include "Chain.h"
#include "Helper.h"
#include "trees/QuART_lil.h"
#include "trees/QuART_tail.h"

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
    bool verbose = false;      // optional argument
    int N = 5000000;           // optional argument
    string input_file;         // required argument
    string tree_type = "ART";  // default tree type

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
        } else if (string(argv[i]) == "-t") {
            tree_type = argv[i + 1];
            i += 2;
        } else {
            i++;
        }
    }

    // read data
    auto keys = read_bin<uint32_t>(input_file.c_str());

    // Build tree
    ART::ART* tree = nullptr;
    if (tree_type == "ART") {
        tree = new ART::ART();
    } else if (tree_type == "QuART_tail") {
        tree = new ART::QuART_tail();
    } else if (tree_type == "QuART_lil") {
        tree = new ART::QuART_lil();
    } else {
        cerr << "Unknown tree type: " << tree_type << endl;
        return 1;
    }

    if (verbose) {
        cout << "Tree type: " << tree_type << endl;
    }

    long long insertion_time = 0;
    for (uint64_t i = 0; i < N; i++) {
        uint8_t key[4];
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

    // Query 1% of entries
    uint64_t minval = 1;
    uint64_t maxval = N;
    srand(time(0));

    long long query_time = 0;
    for (uint64_t i = 0; i < (N / 100); i++) {
        int random = rand() % (maxval - minval + 1) + minval;
        uint8_t key[4];
        ART::loadKey(keys[random], key);
        auto start = chrono::high_resolution_clock::now();
        ART::ArtNode* leaf = tree->lookup(key);
        auto stop = chrono::high_resolution_clock::now();
        auto duration =
            chrono::duration_cast<chrono::nanoseconds>(stop - start);
        query_time += duration.count();
        assert(ART::isLeaf(leaf) && ART::getLeafValue(leaf) == keys[random]);
    }

    if (verbose) {
        cout << "Query time: " << query_time << " ns" << endl;
    }

    // Output the times in csv format, including tree type
    cout << insertion_time << "," << query_time << endl;

    return 0;
}
