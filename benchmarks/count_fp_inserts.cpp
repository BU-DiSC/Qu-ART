#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <vector>

#include "ART.h"
#include "QuARTVariants/QuART_tail.h"
#include "QuARTVariants/QuART_xtail.h" 
#include "QuARTVariants/QuART_lil.h"
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
    bool verbose = false;      // optional argument
    int N = 50000000;           // optional argument
    string input_file;         // required argument
    string tree_type = "QuART_stail";  // default tree type

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
    if (tree_type == "ART") {
        ART::ART tree;
        for (uint64_t i = 0; i < N; i++) {
            uint8_t key[4];
            ART::loadKey(keys[i], key);
            tree.insert(key, keys[i]);
        }
        cout << "number of fast path inserts in " << tree_type << ": " << tree.number_of_fp_inserts << endl;
        cout << "number of root inserts in " << tree_type << ": " << tree.number_of_root_inserts << endl;
        cout << tree.number_of_fp_inserts << "," << tree.number_of_root_inserts << endl;
    } else if (tree_type == "QuART_tail") {
        ART::QuART_tail tree;
        for (uint64_t i = 0; i < N; i++) {
            uint8_t key[4];
            ART::loadKey(keys[i], key);
            tree.insert(key, keys[i]);
        }
        cout << "number of fast path inserts in " << tree_type << ": " << tree.number_of_fp_inserts << endl;
        cout << "number of root inserts in " << tree_type << ": " << tree.number_of_root_inserts << endl;
        cout << tree.number_of_fp_inserts << "," << tree.number_of_root_inserts << endl;
    } else if (tree_type == "QuART_lil") {
        ART::QuART_lil tree;
        for (uint64_t i = 0; i < N; i++) {
            uint8_t key[4];
            ART::loadKey(keys[i], key);
            tree.insert(key, keys[i]);
        }
        cout << "number of fast path inserts in " << tree_type << ": " << tree.number_of_fp_inserts << endl;
        cout << "number of root inserts in " << tree_type << ": " << tree.number_of_root_inserts << endl;
        cout << tree.number_of_fp_inserts << "," << tree.number_of_root_inserts << endl;
    } else if (tree_type == "QuART_stail") {
        ART::QuART_stail tree;
        for (uint64_t i = 0; i < N; i++) {
            uint8_t key[4];
            ART::loadKey(keys[i], key);
            tree.insert(key, keys[i]);
        }
        cout << "number of fast path inserts in " << tree_type << ": " << tree.number_of_fp_inserts << endl;
        cout << "number of root inserts in " << tree_type << ": " << tree.number_of_root_inserts << endl;
        cout << tree.number_of_fp_inserts << "," << tree.number_of_root_inserts << endl;
    } else {
        cerr << "Unknown tree type: " << tree_type << endl;
        return 1;
    }

    return 0;
}
