
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <vector>

#include "ART.h"
#include "ART_tail.h"
#include "ArtNode.h"
#include "ArtNodeTail.cpp"
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

    ART::ART_tail* tree = new ART::ART_tail();
    long long insertion_time = 0;
    for (uint64_t i = 0; i < N; i++) {
        uint8_t key[4];
        ART::loadKey(keys[i], key);
        auto start = chrono::high_resolution_clock::now();

        int k = 2;

        if (i == k) {
            cout << "Before inserting " << keys[i] << ", i=" << i << endl;
            cout << "tree" << endl;
            tree->printTree();
            
            cout << "fp_path" << endl;
            printTailPath(tree->fp_path, tree->fp_path_length);
            cout << "prefixes" << endl;
            for (size_t j = 0; j < tree->fp_path_length; j++) {
                cout << "Node " << j << ": ";
                if (ART::isLeaf(tree->fp_path[j])) {
                    cout << "Leaf(" << ART::getLeafValue(tree->fp_path[j]) << ")" << endl;
                } else {
                    cout << "Prefix: ";
                    for (size_t k = 0; k < tree->fp_path[j]->prefixLength; k++) {
                        cout << static_cast<int>(tree->fp_path[j]->prefix[k]) << " ";
                    }
                }   
            }
            
            cout << endl;
            cout << endl;
        }
        
        //cout << i << endl;
        tree->insert(key, keys[i]);
        
        if (i == k) {
            cout << "After inserting " << keys[i] << ", i=" << i << endl;
            cout << "tree" << endl;
            tree->printTree();
            
            cout << "fp_path" << endl;
            printTailPath(tree->fp_path, tree->fp_path_length);
            cout << "prefixes" << endl;
            for (size_t j = 0; j < tree->fp_path_length; j++) {
                cout << "Node " << j << ": ";
                if (ART::isLeaf(tree->fp_path[j])) {
                    cout << "Leaf(" << ART::getLeafValue(tree->fp_path[j]) << ")" << endl;
                } else {
                    cout << "Prefix: ";
                    for (size_t k = 0; k < tree->fp_path[j]->prefixLength; k++) {
                        cout << static_cast<int>(tree->fp_path[j]->prefix[k]) << " ";
                    }
                }
            }
            
            cout << endl;
            cout << endl;
        }
        auto stop = chrono::high_resolution_clock::now();
        auto duration =
            chrono::duration_cast<chrono::nanoseconds>(stop - start);
        insertion_time += duration.count();
        
        
        if (!tree->verifyTailPath()) {
            cout << "fp path verification failed at i=" << i << ", keys=" << keys[i] << endl;
            break;
        }
        
        
        
        

        
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
