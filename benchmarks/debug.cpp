#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <vector>

#include "ART.h"
#include "QuARTVariants/QuART_stail.h"
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
    int N = 50000000;       // optional argument
    string input_file;     // required argument
    // Parse arguments; make sure to increment i by 2 if you consume an argument
    for (int i = 1; i < argc;) {
        if (string(argv[i]) == "-N") {
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
    ART::QuART_stail* tree = new ART::QuART_stail();

    for (uint64_t i = 0; i < N; i++) {
        //cout << "inserting " << keys[i] << endl;
        uint8_t key[4];
        ART::loadKey(keys[i], key);
        /*
        if (i == 1794947 || i == 1794946) {
            tree->printTree();
            cout << endl;
            cout << "Inserting key: " << keys[i] << ", i: " << i << endl;
            cout << "fp depth: " << tree->fp_depth << endl;
            cout << "fp_leaf value: " << ART::getLeafValue(tree->fp_leaf) << endl;
            cout << endl;
            tree->insert(key, keys[i]);
            tree->printTree();
        }
        else {
            tree->insert(key, keys[i]);
        }
        */
        tree->insert(key, keys[i]);
        ART::ArtNode* leaf = tree->lookup(key);
        if (!ART::isLeaf(leaf) || ART::getLeafValue(leaf) != keys[i]) {
            cout << "Error for file: "<< input_file <<". leaf value mismatch for key: " << keys[i] << ", i: " << i << endl;
            break;
        }
    }

    cout << "Tree built successfully with " << N << " keys for file " << input_file << endl;

    return 0;
}
