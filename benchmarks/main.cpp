
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <vector>

#include "ART.h"
#include "QuARTVariants/QuART_tail.h"
#include "QuARTVariants/QuART_xtail.h" 
#include "QuARTVariants/QuART_lil.h"
#include "ArtNode.h"
#include "ArtNodeNewMethods.cpp"
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
        } else if (string(argv[i]) == "-f") {
            input_file = argv[i + 1];
            i += 2;
        }
    }

    // read data
    auto keys = read_bin<uint32_t>(input_file.c_str());

    // Build tree

    // Change the type of tree to ART::ART to use ART tree
    ART::QuART_tail* tree = new ART::QuART_tail();

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
        
        // Uncomment the following lines to verify the tail path after each insertion
        /*
        if (!tree->verifyTailPath()) {
            cout << "fp path verification failed at i=" << i << ", keys=" << keys[i] << endl;
            break;
        }
        */
        
    }

    if (verbose) {
        cout << "Insertion time: " << insertion_time << " ns" << endl;
    }

    
    // Query tree
    long long query_time = 0;
    for (uint64_t i = 0; i < N; i++) {
        //cout << i << endl;
        uint8_t key[4];
        ART::loadKey(keys[i], key);
        auto start = chrono::high_resolution_clock::now();
        ART::ArtNode* leaf = tree->lookup(key);
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
