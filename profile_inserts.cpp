#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <vector>

#include "ART.h"
#include "QuART_tail.h"
#include "ArtNode.h"
#include "ArtNodeTail.cpp"
#include "Chain.h"
#include "Helper.h"

using namespace std;

int main(int argc, char** argv) {
    int N = 10000000;       // default value
    string input_file;     // required argument
    // Parse only -N argument
    for (int i = 1; i < argc;) {
        if (string(argv[i]) == "-N" && i + 1 < argc) {
            N = atoi(argv[i + 1]);
            i += 2;
        } else {
            i++;
        }
    }

    // read data
    std::vector<uint32_t> keys(10000000);
    for (uint32_t i = 0; i < 10000000; i++) {
        keys[i] = i + 1;
    }


    // Change the type of tree to ART::ART to use ART tree
    ART::QuART_tail* tree = new ART::QuART_tail();

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
