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
    int N = 50000000;       // default value
    string input_file;      // required argument
    
    for (int i = 1; i < argc;) {
        if (string(argv[i]) == "-N" && i + 1 < argc) {
            N = atoi(argv[i + 1]);
            i += 2;
        } else if (string(argv[i]) == "-f" && i + 1 < argc) {
            input_file = argv[i + 1];
            i += 2;
        } else {
            i++;
        }
    }
    
    // read data
    auto keys = read_bin<uint32_t>(input_file.c_str());

    // Change the type of tree to ART::ART to use ART tree
    ART::QuART_xtail* tree = new ART::QuART_xtail();

    for (uint64_t i = 0; i < N; i++) {
        uint8_t key[4];
        ART::loadKey(keys[i], key);

        tree->insert(key, keys[i]);
                
        // Uncomment the following lines to verify the tail path after each insertion
        /*
        if (!tree->verifyTailPath()) {
            cout << "fp path verification failed at i=" << i << ", keys=" << keys[i] << endl;
            break;
        }
        */    
    }

    return 0;
}