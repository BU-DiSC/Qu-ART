
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <vector>

#include "ArtNode.h"
#include "ArtNode_lil.h"
#include "Chain.h"
#include "Helper.h"
#include "QuART_lil.h"

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
    int N = 5000000;  // optional argument
    // Parse arguments; make sure to increment i by 2 if you consume an argument
    for (int i = 1; i < argc;) {
        if (string(argv[i]) == "-N") {
            N = atoi(argv[i + 1]);
            i += 2;
        } else {
            i++;
        }
    }

    // Build tree

    ART::QuART_lil* tree = new ART::QuART_lil();
    for (uint32_t val = 1; val <= N; val++) {
        uint8_t key[4];
        ART::loadKey(val, key);
        tree->insert(key, val);
    }

    return 0;
}
