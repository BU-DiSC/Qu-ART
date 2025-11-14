#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <vector>

#include "ART.h"
#include "ArtNode.h"
#include "Chain.h"
#include "Helper.h"
#include "trees/QuART_lil.h"
#include "trees/QuART_tail.h"
#include "trees/QuART_stail.h"
#include "trees/QuART_lil_can.h"
#include "trees/QuART_stail_reset.h"

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
    int N = 500000000;         // optional argument
    string input_file;         // required argument

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
        } else {
            cerr << "Unknown argument: " << argv[i] << endl;
            return 1;
        }
    }

    // read data
    auto keys = read_bin<uint32_t>(input_file.c_str());

    if (verbose) {
        cout << "Read " << keys.size() << " keys from " << input_file << endl;
    }

    ART::QuART_stail_reset* tree = new ART::QuART_stail_reset();
    for (uint64_t i = 0; i < N; i++) {
        uint8_t key[4];
        ART::loadKey(keys[i], key);
        tree->insert(key, keys[i]);
    }

    if (verbose) {
        cout << "Inserted " << N << " keys into the tree." << endl;
    }

    // Query all keys for sanity check
    for (uint64_t i = 0; i < N; i++) {
        uint8_t key[4];
        ART::loadKey(keys[i], key);
        ART::ArtNode* leaf = tree->lookup(key);
        assert(ART::isLeaf(leaf) &&
                ART::getLeafValue(leaf) == keys[i]);
    }

    if (verbose) {
        cout << tree->fp_change_count << " path resets occurred during insertions."
             << endl;
    }

    cout << tree->fp_change_count << endl;

    delete tree;
    return 0;
}
