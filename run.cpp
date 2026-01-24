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
#include "trees/QuART_stail_reset_bidir.h"

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
    string tree_type = "ART";  // default tree type

    
    // Query 1% of entries
    uint64_t minval = 0;
    uint64_t maxval = N-1;

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

    if (tree_type == "ART") {
        ART::ART* tree = new ART::ART();
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
        }

        if (verbose) {
            cout << "Tree type: " << tree_type << endl;
            cout << "Insertion time: " << insertion_time << " ns" << endl;
        }

        srand(time(0));

        long long query_time = 0;
        for (uint64_t i = 0; i < (N / 100); i++) {
            int random = rand() % (maxval - minval + 1) + minval;
            uint8_t key[4];
            ART::loadKey(keys[random], key);
            auto start = chrono::high_resolution_clock::now();
            ART::ArtNode* leaf = tree->lookup(key);
            auto stop = chrono::high_resolution_clock::now();
            auto duration =
                chrono::duration_cast<chrono::nanoseconds>(stop - start);
            query_time += duration.count();
            assert(ART::isLeaf(leaf) &&
                   ART::getLeafValue(leaf) == keys[random]);
        }

        if (verbose) {
            cout << "Query time: " << query_time << " ns" << endl;
        }

        // Output the times in csv format, including tree type
        cout << insertion_time << "," << query_time << endl;
    } else if (tree_type == "QuART_tail") {
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
        }

        if (verbose) {
            cout << "Tree type: " << tree_type << endl;
            cout << "Insertion time: " << insertion_time << " ns" << endl;
        }

        srand(time(0));

        long long query_time = 0;
        for (uint64_t i = 0; i < (N / 100); i++) {
            int random = rand() % (maxval - minval + 1) + minval;
            uint8_t key[4];
            ART::loadKey(keys[random], key);
            auto start = chrono::high_resolution_clock::now();
            ART::ArtNode* leaf = tree->lookup(key);
            auto stop = chrono::high_resolution_clock::now();
            auto duration =
                chrono::duration_cast<chrono::nanoseconds>(stop - start);
            query_time += duration.count();
            assert(ART::isLeaf(leaf) &&
                   ART::getLeafValue(leaf) == keys[random]);
        }

        if (verbose) {
            cout << "Query time: " << query_time << " ns" << endl;
        }

        // Output the times in csv format, including tree type
        cout << insertion_time << "," << query_time << endl;
    } else if (tree_type == "QuART_lil") {
        ART::QuART_lil* tree = new ART::QuART_lil();
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
        }

        if (verbose) {
            cout << "Tree type: " << tree_type << endl;
            cout << "Insertion time: " << insertion_time << " ns" << endl;
        }

        srand(time(0));

        long long query_time = 0;
        for (uint64_t i = 0; i < (N / 100); i++) {
            int random = rand() % (maxval - minval + 1) + minval;
            uint8_t key[4];
            ART::loadKey(keys[random], key);
            auto start = chrono::high_resolution_clock::now();
            ART::ArtNode* leaf = tree->lookup(key);
            auto stop = chrono::high_resolution_clock::now();
            auto duration =
                chrono::duration_cast<chrono::nanoseconds>(stop - start);
            query_time += duration.count();
            assert(ART::isLeaf(leaf) &&
                   ART::getLeafValue(leaf) == keys[random]);
        }

        if (verbose) {
            cout << "Query time: " << query_time << " ns" << endl;
        }

        // Output the times in csv format, including tree type
        cout << insertion_time << "," << query_time << endl;
    } else if (tree_type == "QuART_stail_reset_bidir") {
        ART::QuART_stail_reset_bidir* tree = new ART::QuART_stail_reset_bidir();
        long long insertion_time = 0;
        for (int64_t i = 0; i < 500000000; i++) {
            /*
            if (keys[i] == 163752268) {
                cout << i << endl;
            }
            */
            //cout << i << " " << keys[i] << endl;
            //cout << "Current leafValue: " << ART::getLeafValue(tree->fp_leaf) << " " << i << " " << keys[i] << endl;
            uint8_t key[4];
            ART::loadKey(keys[i], key);
            auto start = chrono::high_resolution_clock::now();

            //tree->printTree();
            
            /*
            // print fp path
            printf("fp_path_length: %zu\n", tree->fp_path_length);
            printf("fp_path: ");
            for (size_t j = 0; j < tree->fp_path_length; j++) {
                printf("%p ", (void*)tree->fp_path[j]);
            }
            printf("\n");
            */
            
            tree->insert(key, keys[i]);
            auto stop = chrono::high_resolution_clock::now();
            auto duration =
                chrono::duration_cast<chrono::nanoseconds>(stop - start);
            insertion_time += duration.count();
        }

        /*
        // insert for i = 188496940, 188496941, 188496942, 188496943
        for (int64_t i = 188496935; i < 188498000; i++) {
            //cout << i << " " << keys[i] << endl;
            cout << "Current leafValue: " << ART::getLeafValue(tree->fp_leaf) << " i: " << i << ", keys[i]: " << keys[i] << endl;
            cout << "Direction: " << tree->dir << endl;
            cout << "Reset counter : " << tree->reset_counter << endl;
            cout << "Number of fp inserts: " << tree->number_of_fp_inserts << endl;
            cout << "Number of top inserts: " << tree->number_of_top_inserts << endl;
            cout << endl;
            uint8_t key[4];
            ART::loadKey(keys[i], key);
            auto start = chrono::high_resolution_clock::now();

            //tree->printTree();
            
            // print fp path
            printf("fp_path_length: %zu\n", tree->fp_path_length);
            printf("fp_path: ");
            for (size_t j = 0; j < tree->fp_path_length; j++) {
                printf("%p ", (void*)tree->fp_path[j]);
            }
            printf("\n");
            
            tree->insert(key, keys[i]);
            auto stop = chrono::high_resolution_clock::now();
            auto duration =
                chrono::duration_cast<chrono::nanoseconds>(stop - start);
            insertion_time += duration.count();
        }
        */
        /*
        for (int64_t i = 300000000; i < N; i++) {
            //cout << tree->dir << endl;
            //cout << i << " " << keys[i] << endl;
            cout << "Current leafValue: " << ART::getLeafValue(tree->fp_leaf) << " " << i << " " << keys[i] << endl;
            cout << tree->number_of_fp_inserts << endl;
            cout << ART::getLeafValue(tree->fp_leaf) << endl;
            uint8_t key[4];
            ART::loadKey(keys[i], key);
            auto start = chrono::high_resolution_clock::now();

            //tree->printTree();
            
            // print fp path
            printf("fp_path_length: %zu\n", tree->fp_path_length);
            printf("fp_path: ");
            for (size_t j = 0; j < tree->fp_path_length; j++) {
                printf("%p ", (void*)tree->fp_path[j]);
            }
            printf("\n");
            
            tree->insert(key, keys[i]);
            auto stop = chrono::high_resolution_clock::now();
            auto duration =
                chrono::duration_cast<chrono::nanoseconds>(stop - start);
            insertion_time += duration.count();
        }
        */

        if (verbose) {
            cout << "Tree type: " << tree_type << endl;
            cout << "Insertion time: " << insertion_time << " ns" << endl;
        }

        srand(time(0));

        long long query_time = 0;
        for (uint64_t i = 0; i < (N / 100); i++) {
            int random = rand() % (maxval - minval + 1) + minval;
            uint8_t key[4];
            ART::loadKey(keys[random], key);
            auto start = chrono::high_resolution_clock::now();
            ART::ArtNode* leaf = tree->lookup(key);
            auto stop = chrono::high_resolution_clock::now();
            auto duration =
                chrono::duration_cast<chrono::nanoseconds>(stop - start);
            query_time += duration.count();
            assert(ART::isLeaf(leaf) &&
                   ART::getLeafValue(leaf) == keys[random]);
        }

        if (verbose) {
            cout << "Query time: " << query_time << " ns" << endl;
            cout << "Number of fp inserts: " << tree->number_of_fp_inserts << endl;
            cout << "Number of top inserts: " << tree->number_of_top_inserts << endl;
        }

        // Output the times in csv format, including tree type
        cout << insertion_time << "," << query_time << endl;
    } else if (tree_type == "QuART_lil_can") {
        ART::QuART_lil_can* tree = new ART::QuART_lil_can();
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
        }

        if (verbose) {
            cout << "Tree type: " << tree_type << endl;
            cout << "Insertion time: " << insertion_time << " ns" << endl;
        }

        srand(time(0));

        long long query_time = 0;
        for (uint64_t i = 0; i < (N / 100); i++) {
            int random = rand() % (maxval - minval + 1) + minval;
            uint8_t key[4];
            ART::loadKey(keys[random], key);
            auto start = chrono::high_resolution_clock::now();
            ART::ArtNode* leaf = tree->lookup(key);
            auto stop = chrono::high_resolution_clock::now();
            auto duration =
                chrono::duration_cast<chrono::nanoseconds>(stop - start);
            query_time += duration.count();
            assert(ART::isLeaf(leaf) &&
                   ART::getLeafValue(leaf) == keys[random]);
        }

        if (verbose) {
            cout << "Query time: " << query_time << " ns" << endl;
        }

        // Output the times in csv format, including tree type
        cout << insertion_time << "," << query_time << endl;
    } else if (tree_type == "QuART_stail_reset") {
        ART::QuART_stail_reset* tree = new ART::QuART_stail_reset();
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
        }

        if (verbose) {
            cout << "Tree type: " << tree_type << endl;
            cout << "Insertion time: " << insertion_time << " ns" << endl;
        }

        srand(time(0));

        long long query_time = 0;
        for (uint64_t i = 0; i < (N / 100); i++) {
            int random = rand() % (maxval - minval + 1) + minval;
            uint8_t key[4];
            ART::loadKey(keys[random], key);
            auto start = chrono::high_resolution_clock::now();
            ART::ArtNode* leaf = tree->lookup(key);
            auto stop = chrono::high_resolution_clock::now();
            auto duration =
                chrono::duration_cast<chrono::nanoseconds>(stop - start);
            query_time += duration.count();
            assert(ART::isLeaf(leaf) &&
                   ART::getLeafValue(leaf) == keys[random]);
        }

        if (verbose) {
            cout << "Query time: " << query_time << " ns" << endl;
        }

        // Output the times in csv format, including tree type
        cout << insertion_time << "," << query_time << endl;
    }
    else {
        cerr << "Unknown tree type: " << tree_type << endl;
        return 1;
    }
    return 0;
}
