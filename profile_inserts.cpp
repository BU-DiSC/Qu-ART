
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

/*
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
*/

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
        } 
        /*else if (string(argv[i]) == "-f") {
            input_file = argv[i + 1];
            i += 2;
        }*/
    }

    // read data
    //auto keys = read_bin<uint32_t>(input_file.c_str());

    std::vector<uint32_t> keys(10000000);
    for (uint32_t i = 0; i < 10000000; ++i) {
        keys[i] = i + 1;
    }


    // Change the type of tree to ART::ART to use ART tree
    ART::ART* tree = new ART::ART();

    for (uint64_t i = 0; i < N; i++) {
        uint8_t key[4];
        ART::loadKey(keys[i], key);

        tree->insert(key, keys[i]);

        //cout << i << endl;
                
        // Uncomment the following lines to verify the tail path after each insertion
        /*
        if (!tree->verifyTailPath()) {
            cout << "fp path verification failed at i=" << i << ", keys=" << keys[i] << endl;
            break;
        }
        */    
    }

    //cout << "finished everything" << endl;
    return 0;
}
