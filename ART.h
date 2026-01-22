/*
  Adaptive Radix Tree
  Viktor Leis, 2012
  leis@in.tum.de
 */

#pragma once

#include <assert.h>
#include <emmintrin.h>  // x86 SSE intrinsics
#include <immintrin.h>  // AVX512
#include <stdint.h>     // integer types
#include <stdio.h>
#include <stdlib.h>    // malloc, free
#include <string.h>    // memset, memcpy
#include <sys/time.h>  // gettime

#include <algorithm>  // std::random_shuffle
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <locale>
#include <memory>
#include <stdexcept>

#include "ArtNode.h"  // ArtNode definitions
#include "Chain.h"    // Chain definitions
#include "Helper.h"   // Helper functions

namespace ART {

class ART {
   public:
    ArtNode* root;  // pointer to root node of tree
    ArtNode* fp;    // pointer to fast path node
    std::array<ArtNode*, maxPrefixLength> fp_path;  // path that leads to fp
    std::array<ArtNode**, maxPrefixLength>
        fp_path_ref;        // references to nodes on fp_path
    size_t fp_path_length;  // stores length of fp path
    ArtNode* fp_leaf;       // pointer to leaf node in fast path
    size_t fp_depth;        // depth that will be used during fp insertion
    ArtNode** fp_ref;       // reference to fp node, used for insertion

    // constructor
    ART()
        : root(nullptr),
          fp(nullptr),
          fp_path{nullptr},
          fp_path_length(0),
          fp_leaf(nullptr),
          fp_depth(0),
          fp_ref(nullptr) {}

    void insert(uint8_t key[], uintptr_t value) {
        insert(this, root, &root, key, 0, value, maxPrefixLength);
    }

    ArtNode* lookup(uint8_t key[]) {
        return lookup(root, key, maxPrefixLength, 0, maxPrefixLength);
    }

    Chain* rangelookup(uint8_t l_key[], unsigned l_keyLength, uint8_t h_key[],
                       uint8_t h_keyLength, unsigned maxKeyLength) {
        return rangelookup(root, l_key, l_keyLength, h_key, h_keyLength,
                           maxKeyLength);
    }

    void printTree() { printTree(this->root, 0); }

    // Method to verify the tail path after each insertion
    // Returns true if the fast path (fp_path) leads to the correct fp and
    // fp_leaf
    bool verifyTailPath() {
        if (this->fp_path_length == 0) {
            // No fast path to verify
            return true;
        }

        ArtNode* current = this->root;
        // Traverse the tree following the fp_path
        for (size_t i = 0; i < this->fp_path_length; i++) {
            // If we're at the last node in the fp_path, check if it's the fp
            // node
            if (i == this->fp_path_length - 1) {
                if (current == this->fp) {
                    // Check if the leaf value matches the expected fp_leaf
                    if (getLeafValue(maximum(current)) ==
                        getLeafValue(this->fp_leaf)) {
                        return true;
                    } else {
                        std::cerr << "Error: fp_leaf mismatch. Expected "
                                  << getLeafValue(maximum(current)) << ", got "
                                  << getLeafValue(this->fp_leaf) << "."
                                  << std::endl;
                        return false;
                    }
                } else {
                    std::cerr << "Error: last node in fp_path is not the fp. "
                                 "Expected "
                              << static_cast<void*>(current) << ", got "
                              << static_cast<void*>(this->fp) << "."
                              << std::endl;
                    return false;
                }
            }

            // Move to the rightmost child for each node type
            switch (current->type) {
                case NodeType4: {
                    Node4* node = static_cast<Node4*>(current);
                    if (node->count > 0) {
                        // Move to the last child (rightmost)
                        current = node->child[node->count - 1];
                    } else {
                        std::cerr << "Error: NodeType4 has no children."
                                  << std::endl;
                        return false;
                    }
                    break;
                }
                case NodeType16: {
                    Node16* node = static_cast<Node16*>(current);
                    if (node->count > 0) {
                        // Move to the last child (rightmost)
                        current = node->child[node->count - 1];
                    } else {
                        std::cerr << "Error: NodeType16 has no children."
                                  << std::endl;
                        return false;
                    }
                    break;
                }
                case NodeType48: {
                    Node48* node = static_cast<Node48*>(current);
                    unsigned pos = 255;
                    // Find the rightmost valid child
                    while (pos > 0 && node->childIndex[pos] == emptyMarker)
                        pos--;
                    if (node->childIndex[pos] != emptyMarker) {
                        current = node->child[node->childIndex[pos]];
                    } else {
                        std::cerr << "Error: NodeType48 has no valid children."
                                  << std::endl;
                        return false;
                    }
                    break;
                }
                case NodeType256: {
                    Node256* node = static_cast<Node256*>(current);
                    unsigned pos = 255;
                    // Find the rightmost valid child
                    while (pos > 0 && !node->child[pos]) pos--;
                    if (node->child[pos]) {
                        current = node->child[pos];
                    } else {
                        std::cerr << "Error: NodeType256 has no valid children."
                                  << std::endl;
                        return false;
                    }
                    break;
                }
                default:
                    std::cerr << "Error: Unknown node type." << std::endl;
                    return false;
            }
        }

        // If we exit the loop without returning, the path is incorrect
        std::cerr << "Error: fp_path does not lead to the fp." << std::endl;
        return false;
    }

    void bulkLoad(const std::vector<uint32_t>& keys, const std::vector<uint32_t>& values) {
        ArtNode* bl_ptr = nullptr; // pointer to current bulk load node
        ArtNode** bl_ptr_ref = &this->root; // reference to current bulk load node
        std::array<uint8_t, 3> bl_pf_bytes; // key bytes for d1, d2, d3 nodes

        // Calculate number of complete groups of 256
        size_t num_complete_groups = keys.size() / 256;
        size_t remaining_keys = keys.size() % 256;
        // Special case: handle first group when root is null
        if (root == nullptr) {
            // Create root -> d1 -> d2 -> d3 (Node256) for first 256 keys
            Node4* root_node = new Node4();
            root_node->prefixLength = 0;
            this->root = root_node;
            
            Node4* d1_node = new Node4();
            d1_node->prefixLength = 0;
            root_node->insertNode4(this, &this->root, 0, d1_node);
            
            Node4* d2_node = new Node4();
            d2_node->prefixLength = 0;
            d1_node->insertNode4(this, findChild(this->root, 0), 0, d2_node);
            
            Node256* d3_node = new Node256();
            d3_node->prefixLength = 0;
            
            d2_node->insertNode4(this, findChild(*findChild(this->root, 0), 0), 0, d3_node);
            
            bl_ptr = d2_node;
            bl_ptr_ref = findChild(*findChild(this->root, 0), 0);
            bl_pf_bytes = {{0, 0, 0}};
            
            // Insert 256 keys into this group
            for (size_t i = 1; i < 256; i++) {
                uint8_t b3 = keys[i] & 0xFF;
                d3_node->child[b3] = makeLeaf(values[i]);
            }
        }

        //this->printTree();
        
        // Process complete groups of 256 keys
        for (size_t group = 1; group < num_complete_groups; group++) {
            size_t start_idx = group * 256;
            
            // Separate start_idx into big endian bytes
            uint8_t b0 = (start_idx >> 24) & 0xFF;
            uint8_t b1 = (start_idx >> 16) & 0xFF;
            uint8_t b2 = (start_idx >> 8) & 0xFF;

            // Bridge at byte 0: b0 changed, b1=0, b2=0, previous b0 was at max (255)
            if (b0 != bl_pf_bytes[0] && b1 == 0 && b2 == 0) {
                Node4* new_d1_node = new Node4();
                new_d1_node->prefixLength = 0;
                switch (this->root->type) {
                    case NodeType4:
                        static_cast<Node4*>(this->root)->bulkLoadInsertNode4(this, &this->root, b0, new_d1_node, bl_ptr);
                        break;
                    case NodeType16:
                        static_cast<Node16*>(this->root)->bulkLoadInsertNode16(this, &this->root, b0, new_d1_node, bl_ptr);
                        break;
                    case NodeType48:
                        static_cast<Node48*>(this->root)->bulkLoadInsertNode48(this, &this->root, b0, new_d1_node, bl_ptr);
                        break;
                    case NodeType256:
                        static_cast<Node256*>(this->root)->bulkLoadInsertNode256(this, &this->root, b0, new_d1_node, bl_ptr);
                        break;
                }
                // insert a d2 node under new d1 node
                ArtNode** d1_ref = findChild(this->root, b0);
                ArtNode* d1_node = *d1_ref;
                Node4* new_d2_node = new Node4();
                new_d2_node->prefixLength = 0;
                static_cast<Node4*>(d1_node)->bulkLoadInsertNode4(this, d1_ref, b1, new_d2_node, bl_ptr);
                // Update bl_ptr to new d2 node
                bl_ptr = new_d2_node;
                bl_ptr_ref = findChild(*d1_ref, b1);
                bl_pf_bytes = {b0, 0, 0};

                // insert the grouped keys into d3 node under new d2 node
                Node256* d3_node = new Node256();
                d3_node->prefixLength = 0;
                static_cast<Node4*>(new_d2_node)->bulkLoadInsertNode4(this, bl_ptr_ref, b2, d3_node, bl_ptr);
                // Insert 256 keys into d3 node
                for (size_t i = 0; i < 256; i++) {
                    uint8_t b3 = keys[start_idx + i] & 0xFF;
                    d3_node->child[b3] = makeLeaf(values[start_idx + i]);
                }
                continue;
            }
            // Bridge at byte 1: b0 same, b1 changed, b2=0, previous b1 was at max
            else if (b0 == bl_pf_bytes[0] && b1 != bl_pf_bytes[1] && b2 == 0) {
                ArtNode** d1_ref = findChild(this->root, b0);
                ArtNode* d1_node = *d1_ref;
                // Create new d2 node
                Node4* new_d2_node = new Node4();
                new_d2_node->prefixLength = 0;
                switch (d1_node->type) {
                    case NodeType4:
                        static_cast<Node4*>(d1_node)->bulkLoadInsertNode4(this, d1_ref, b1, new_d2_node, bl_ptr);
                        break;
                    case NodeType16:
                        static_cast<Node16*>(d1_node)->bulkLoadInsertNode16(this, d1_ref, b1, new_d2_node, bl_ptr);
                        break;
                    case NodeType48:
                        static_cast<Node48*>(d1_node)->bulkLoadInsertNode48(this, d1_ref, b1, new_d2_node, bl_ptr);
                        break;
                    case NodeType256:
                        static_cast<Node256*>(d1_node)->bulkLoadInsertNode256(this, d1_ref, b1, new_d2_node, bl_ptr);
                        break;
                }
                // Update bl_ptr to new d2 node
                bl_ptr = new_d2_node;
                bl_ptr_ref = findChild(*d1_ref, b1);
                bl_pf_bytes[1] = b1;
                bl_pf_bytes[2] = 0;
                
                // insert the grouped keys into d3 node under new d2 node
                Node256* d3_node = new Node256();
                d3_node->prefixLength = 0;
                static_cast<Node4*>(new_d2_node)->bulkLoadInsertNode4(this, bl_ptr_ref, b2, d3_node, bl_ptr);
                // Insert 256 keys into d3 node
                for (size_t i = 0; i < 256; i++) {
                    uint8_t b3 = keys[start_idx + i] & 0xFF;
                    d3_node->child[b3] = makeLeaf(values[start_idx + i]);
                }              
                continue;
            }

            // Navigate from bl_ptr (d2 node) to get/create d3 node at position b2
            ArtNode** d3_ref = findChild(bl_ptr, b2);
            Node256* d3_node;
            
            if (*d3_ref == nullptr) {
                // Create new d3 node
                d3_node = new Node256();
                d3_node->prefixLength = 0;
                // Insert into bl_ptr (d2 node)
                switch (bl_ptr->type) {
                    case NodeType4:
                        static_cast<Node4*>(bl_ptr)->bulkLoadInsertNode4(this, bl_ptr_ref, b2, d3_node, bl_ptr);
                        break;
                    case NodeType16:
                        static_cast<Node16*>(bl_ptr)->bulkLoadInsertNode16(this, bl_ptr_ref, b2, d3_node, bl_ptr);
                        break;
                    case NodeType48:
                        static_cast<Node48*>(bl_ptr)->bulkLoadInsertNode48(this, bl_ptr_ref, b2, d3_node, bl_ptr);
                        break;
                    case NodeType256:
                        static_cast<Node256*>(bl_ptr)->bulkLoadInsertNode256(this, bl_ptr_ref, b2, d3_node, bl_ptr);
                        break;
                }
                // Re-get the reference after potential expansion
                d3_ref = findChild(bl_ptr, b2);
                d3_node = static_cast<Node256*>(*d3_ref);
            } else {
                d3_node = static_cast<Node256*>(*d3_ref);
            }
            
            // Insert 256 keys into d3 node
            for (size_t i = 0; i < 256; i++) {
                uint8_t b3 = keys[start_idx + i] & 0xFF;
                d3_node->child[b3] = makeLeaf(values[start_idx + i]);
            }
            
            // Update bl_pf_bytes to track current position
            bl_pf_bytes[2] = b2;

            //this->printTree();
            
        }
        
        //this->printTree();
        return;
    }

   private:
    // Void insert function
    void insert(ART* tree, ArtNode* node, ArtNode** nodeRef, uint8_t key[],
                unsigned depth, uintptr_t value, unsigned maxKeyLength) {
        // Insert the leaf value into the tree

        if (node == NULL) {
            *nodeRef = makeLeaf(value);
            return;
        }

        if (isLeaf(node)) {
            // Replace leaf with Node4 and store both leaves in it
            uint8_t existingKey[maxKeyLength];
            loadKey(getLeafValue(node), existingKey);
            unsigned newPrefixLength = 0;
            while (existingKey[depth + newPrefixLength] ==
                   key[depth + newPrefixLength])
                newPrefixLength++;

            Node4* newNode = new Node4();
            newNode->prefixLength = newPrefixLength;
            memcpy(newNode->prefix, key + depth,
                   min(newPrefixLength, maxPrefixLength));
            *nodeRef = newNode;

            newNode->insertNode4(this, nodeRef,
                                 existingKey[depth + newPrefixLength], node);
            newNode->insertNode4(this, nodeRef, key[depth + newPrefixLength],
                                 makeLeaf(value));
            return;
        }

        // Handle prefix of inner node
        if (node->prefixLength) {
            unsigned mismatchPos =
                prefixMismatch(node, key, depth, maxKeyLength);
            if (mismatchPos != node->prefixLength) {
                // Prefix differs, create new node
                Node4* newNode = new Node4();
                *nodeRef = newNode;
                newNode->prefixLength = mismatchPos;
                memcpy(newNode->prefix, node->prefix,
                       min(mismatchPos, maxPrefixLength));
                // Break up prefix
                if (node->prefixLength < maxPrefixLength) {
                    newNode->insertNode4(this, nodeRef,
                                         node->prefix[mismatchPos], node);
                    node->prefixLength -= (mismatchPos + 1);
                    memmove(node->prefix, node->prefix + mismatchPos + 1,
                            min(node->prefixLength, maxPrefixLength));
                } else {
                    node->prefixLength -= (mismatchPos + 1);
                    uint8_t minKey[maxKeyLength];
                    loadKey(getLeafValue(minimum(node)), minKey);
                    newNode->insertNode4(this, nodeRef,
                                         minKey[depth + mismatchPos], node);
                    memmove(node->prefix, minKey + depth + mismatchPos + 1,
                            min(node->prefixLength, maxPrefixLength));
                }
                newNode->insertNode4(this, nodeRef, key[depth + mismatchPos],
                                     makeLeaf(value));
                return;
            }
            depth += node->prefixLength;
        }

        // Recurse
        ArtNode** child = findChild(node, key[depth]);
        if (*child) {
            insert(tree, *child, child, key, depth + 1, value, maxKeyLength);
            return;
        }

        // Insert leaf into inner node
        ArtNode* newNode = makeLeaf(value);
        switch (node->type) {
            case NodeType4:
                static_cast<Node4*>(node)->insertNode4(this, nodeRef,
                                                       key[depth], newNode);
                break;
            case NodeType16:
                static_cast<Node16*>(node)->insertNode16(this, nodeRef,
                                                         key[depth], newNode);
                break;
            case NodeType48:
                static_cast<Node48*>(node)->insertNode48(this, nodeRef,
                                                         key[depth], newNode);
                break;
            case NodeType256:
                static_cast<Node256*>(node)->insertNode256(this, nodeRef,
                                                           key[depth], newNode);
                break;
        }
    }

    // Lookup function, returns ArtNode
    ArtNode* lookup(ArtNode* node, uint8_t key[], unsigned keyLength,
                    unsigned depth, unsigned maxKeyLength) {
        // Find the node with a matching key, optimistic version

        bool skippedPrefix = false;  // Did we optimistically skip some prefix
                                     // without checking it?

        while (node != NULL) {
            if (isLeaf(node)) {
                if (!skippedPrefix && depth == keyLength)  // No check required
                    return node;

                if (depth != keyLength) {
                    // Check leaf
                    uint8_t leafKey[maxKeyLength];
                    loadKey(getLeafValue(node), leafKey);
                    for (unsigned i = (skippedPrefix ? 0 : depth);
                         i < keyLength; i++)
                        if (leafKey[i] != key[i]) return NULL;
                }
                return node;
            }

            if (node->prefixLength) {
                if (node->prefixLength < maxPrefixLength) {
                    for (unsigned pos = 0; pos < node->prefixLength; pos++)
                        if (key[depth + pos] != node->prefix[pos]) return NULL;
                } else
                    skippedPrefix = true;
                depth += node->prefixLength;
            }

            node = *findChild(node, key[depth]);
            depth++;
        }

        return NULL;
    }

    // Erase function, deletes a leaf from the tree
    void erase(ArtNode* node, ArtNode** nodeRef, uint8_t key[],
               unsigned keyLength, unsigned depth, unsigned maxKeyLength) {
        // Delete a leaf from a tree

        if (!node) return;

        if (isLeaf(node)) {
            // Make sure we have the right leaf
            if (leafMatches(node, key, keyLength, depth, maxKeyLength))
                *nodeRef = NULL;
            return;
        }

        // Handle prefix
        if (node->prefixLength) {
            if (prefixMismatch(node, key, depth, maxKeyLength) !=
                node->prefixLength)
                return;
            depth += node->prefixLength;
        }

        ArtNode** child = findChild(node, key[depth]);
        if (isLeaf(*child) &&
            leafMatches(*child, key, keyLength, depth, maxKeyLength)) {
            // Leaf found, delete it in inner node
            switch (node->type) {
                case NodeType4:
                    static_cast<Node4*>(node)->eraseNode4(this, nodeRef, child);
                    break;
                case NodeType16:
                    static_cast<Node16*>(node)->eraseNode16(this, nodeRef,
                                                            child);
                    break;
                case NodeType48:
                    static_cast<Node48*>(node)->eraseNode48(this, nodeRef,
                                                            key[depth]);
                    break;
                case NodeType256:
                    static_cast<Node256*>(node)->eraseNode256(this, nodeRef,
                                                              key[depth]);
                    break;
            }
        } else {
            // Recurse
            erase(*child, child, key, keyLength, depth + 1, maxKeyLength);
        }
    }

    // Range lookup function, returns a Chain of ArtNode
    Chain* rangelookup(ArtNode* node, uint8_t l_key[], unsigned l_keyLength,
                       uint8_t h_key[], uint8_t h_keyLength,
                       unsigned maxKeyLength) {
        // Find the node with a matching key, optimistic version
        Chain* queue =
            new Chain((ChainItem*)new ChainItemWithDepth(node, 0, true, true));
        Chain* result = new Chain();

        while (!queue->isEmpty()) {
            ChainItemWithDepth* item = (ChainItemWithDepth*)queue->pop_front();
            node = item->nodeptr();

            int depth = item->depth_;
            bool lequ = item->lequ_, hequ = item->hequ_;
            bool continue_flag =
                0;  // true means the range vialates the key range
            unsigned pos;
            auto compare_and_set = [&](unsigned pos,
                                       uint8_t compared_byte) -> void {
                uint8_t lkey = pos >= l_keyLength ? 0 : l_key[pos];
                uint8_t hkey = pos >= h_keyLength ? 0 : l_key[pos];

                if (lkey < compared_byte)
                    lequ = 0;
                else if (lkey > compared_byte)
                    continue_flag = 1;

                if (hkey < compared_byte)
                    continue_flag = 1;
                else if (hkey > compared_byte)
                    hequ = 0;
            };
            if (isLeaf(node)) {
                uint8_t leafKey[maxKeyLength];
                loadKey(getLeafValue(node), leafKey);
                for (unsigned i = depth;
                     i < maxKeyLength && !continue_flag && (lequ || hequ); i++)
                    compare_and_set(i, leafKey[i]);
                if (!continue_flag) {
                    result->extend_item(new ChainItem(node));
                }
                continue;
            }

            if (node->prefixLength > maxPrefixLength) {
                for (pos = 0;
                     pos < maxPrefixLength && !continue_flag && (lequ || hequ);
                     pos++) {
                    compare_and_set(depth + pos, node->prefix[pos]);
                }
                uint8_t minKey[maxKeyLength];
                loadKey(getLeafValue(minimum(node)), minKey);
                for (; pos < node->prefixLength && !continue_flag &&
                       (lequ || hequ);
                     pos++) {
                    compare_and_set(depth + pos, minKey[depth + pos]);
                }
            } else {
                for (pos = 0; pos < node->prefixLength && !continue_flag &&
                              (lequ || hequ);
                     pos++) {
                    compare_and_set(depth + pos, node->prefix[pos]);
                }
            }
            if (continue_flag) continue;
            depth += node->prefixLength;

            std::unique_ptr<Chain> newly_added =
                std::move(std::unique_ptr<Chain>(newly_added->findChildbyRange(
                    item->nodeptr(), lequ ? l_key[depth] : 0,
                    hequ ? h_key[depth] : 255, depth, lequ, hequ)));
            queue->extend(std::move(newly_added));
        }
        delete queue;
        return result;
    }

    void printTree(ArtNode* node, int depth) {
        if (!node) return;

        // Indent based on depth
        for (int i = 0; i < depth; i++) {
            printf("  ");
        }

        if (isLeaf(node)) {
            printf("Leaf(%lu)\n", getLeafValue(node));
            return;
        }

        switch (node->type) {
            case NodeType4: {
                Node4* n = static_cast<Node4*>(node);
                printf("Node4 [%p]\n", static_cast<void*>(n));
                for (unsigned i = 0; i < n->count; i++) {
                    printTree(n->child[i], depth + 1);
                }
                break;
            }
            case NodeType16: {
                Node16* n = static_cast<Node16*>(node);
                printf("Node16 [%p]\n", static_cast<void*>(n));
                for (unsigned i = 0; i < n->count; i++) {
                    printTree(n->child[i], depth + 1);
                }
                break;
            }
            case NodeType48: {
                Node48* n = static_cast<Node48*>(node);
                printf("Node48 [%p]\n", static_cast<void*>(n));
                for (unsigned i = 0; i < 256; i++) {
                    if (n->childIndex[i] != emptyMarker) {
                        printTree(n->child[n->childIndex[i]], depth + 1);
                    }
                }
                break;
            }
            case NodeType256: {
                Node256* n = static_cast<Node256*>(node);
                printf("Node256 [%p]\n", static_cast<void*>(n));
                for (unsigned i = 0; i < 256; i++) {
                    if (n->child[i]) {
                        printTree(n->child[i], depth + 1);
                    }
                }
                break;
            }
        }
    }
};
}  // namespace ART