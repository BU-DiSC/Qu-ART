
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <vector>

#include "ART.h"
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

    // read data
    std::vector<uint32_t> keys = {133121, 197127, 197384, 197129};
    uint64_t N = keys.size();

    // Build tree
    ART::ART* tree = new ART::ART();
    
    uint64_t i = 0;
    for (i; i < N - 1; i++) {
        uint8_t key[4];
        ART::loadKey(keys[i], key);
        tree->insert(key, keys[i]);
    }
    tree->printTree();

    uint8_t key[4];
    ART::loadKey(keys[i], key);
    tree->insert(key, keys[i]);
    tree->printTree();
    

    
    // Query tree
    for (uint64_t i = 0; i < N; i++) {
        //cout << i << endl;
        uint8_t key[4];
        ART::loadKey(keys[i], key);
        ART::ArtNode* leaf = tree->lookup(key);
        assert(ART::isLeaf(leaf) && ART::getLeafValue(leaf) == keys[i]);
    }
    
    return 0;
}
