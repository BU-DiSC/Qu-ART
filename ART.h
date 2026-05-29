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
#include "Helper.h"   // Helper functions

namespace ART {

// Groups the five FP tracking fields for one workload fingerprint.
struct FpSlot {
    ArtNode*  fp       = nullptr;
    ArtNode*  fp_prev  = nullptr;
    ArtNode*  fp_leaf  = nullptr;
    size_t    fp_depth = 0;
    ArtNode** fp_ref   = nullptr;
    // Cached copy of fp->type to avoid dereferencing fp on every dispatch.
    // Updated wherever fp is assigned (saveToSlot and hook callbacks).
    uint8_t   fp_type  = 0;
    // Cached result of getLeafUpperBytes(getLeafValue(fp_leaf)).
    // Avoids recomputing in the findSlot hot loop.
    // fp_leaf changes only in insert_recursive_change_fp paths, so this
    // is updated once per BRIDGE/NO_MATCH insert (in saveToSlot).
    key_int_t cached_upper = 0;
};

class ART {
   public:
    ArtNode* root;     // pointer to root node of tree
    ArtNode* fp;       // pointer to fast path node
    ArtNode* fp_prev;  // parent node of fp (holds the cell fp_ref points into)
    ArtNode* fp_leaf;  // pointer to leaf node in fast path
    size_t fp_depth;   // depth that will be used during fp insertion
    ArtNode** fp_ref;  // reference to fp node, used for insertion

    // constructor
    ART()
        : root(nullptr),
          fp(nullptr),
          fp_prev(nullptr),
          fp_leaf(nullptr),
          fp_depth(0),
          fp_ref(nullptr) {}

    void insert(uint8_t key[], uintptr_t value) {
        insert(this, root, &root, key, 0, value, maxPrefixLength);
    }

    ArtNode* lookup(uint8_t key[]) {
        return lookup(root, key, maxPrefixLength, 0, maxPrefixLength);
    }

    void printTree() { printTree(this->root, 0); }

    // Method to verify the fast path after each insertion.
    // Returns true if fp has fp_leaf as its maximum leaf.
    bool verifyTailPath() {
        if (this->fp == nullptr) {
            return true;
        }
        if (getLeafValue(maximum(this->fp)) == getLeafValue(this->fp_leaf)) {
            return true;
        }
        std::cerr << "Error: fp_leaf mismatch. Expected "
                  << getLeafValue(maximum(this->fp)) << ", got "
                  << getLeafValue(this->fp_leaf) << "." << std::endl;
        return false;
    }

    // Bulk load optimized for sorted 32-bit keys.
    // NOTE: This implementation is 32-bit-key-specific (fixed 4-level tree
    // structure).  Calling it with QUART_KEY_64 defined produces incorrect
    // results; use the regular insert() path for 64-bit keys instead.
    void bulkLoad(const std::vector<key_int_t>& keys, const std::vector<key_int_t>& values) {
        ArtNode* bl_ptr = nullptr; // pointer to current bulk load node
        ArtNode** bl_ptr_ref = &this->root; // reference to current bulk load node
        std::array<uint8_t, 3> bl_pf_bytes; // key bytes for d1, d2, d3 nodes

        // Calculate number of complete groups of 256
        size_t num_complete_groups = (keys.size()-1) / 256;
        size_t remaining_keys = (keys.size()-1) % 256;
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
        
        // Process complete groups of 256 keys
        for (size_t group = 1; group < num_complete_groups; group++) {
            size_t start_idx = group * 256;
            
            // Separate the first key of this group into big endian bytes
            uint8_t b0 = (keys[start_idx] >> 24) & 0xFF;
            uint8_t b1 = (keys[start_idx] >> 16) & 0xFF;
            uint8_t b2 = (keys[start_idx] >> 8) & 0xFF;

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
        }
        
        // Insert remaining keys that don't make a full group of 256
        size_t start_idx2 = num_complete_groups * 256;
        for (size_t i = 0; i <= remaining_keys; i++) {
            //printf("Inserting remaining key %zu: %u\n", i, keys[start_idx2 + i]);
            uint8_t key_bytes[4];
            key_bytes[0] = (keys[start_idx2 + i] >> 24) & 0xFF;
            key_bytes[1] = (keys[start_idx2 + i] >> 16) & 0xFF;
            key_bytes[2] = (keys[start_idx2 + i] >> 8) & 0xFF;
            key_bytes[3] = keys[start_idx2 + i] & 0xFF;
            insert(this, root, &root, key_bytes, 0, values[start_idx2 + i], maxPrefixLength);
        }
        
        return;
    }

    // Compress the tree by merging nodes with a single child
    int compressTree() {
        int compressed_nodes = 0;
        compressTreeHelper(root, &root, compressed_nodes, 1);
        return compressed_nodes;
    }

   protected:
    // Scans node's child array for target and returns a pointer to that child
    // slot, or nullptr if not found.  Used to recompute fp_ref after a node
    // expansion replaces fp_prev with a larger node type.
    ArtNode** findChildPtr(ArtNode* node, ArtNode* target) {
        switch (node->type) {
            case NodeType4: {
                auto* n = static_cast<Node4*>(node);
                for (int i = 0; i < n->count; i++)
                    if (n->child[i] == target) return &n->child[i];
                break;
            }
            case NodeType16: {
                auto* n = static_cast<Node16*>(node);
                for (int i = 0; i < n->count; i++)
                    if (n->child[i] == target) return &n->child[i];
                break;
            }
            case NodeType48: {
                auto* n = static_cast<Node48*>(node);
                for (int i = 0; i < 48; i++)
                    if (n->child[i] == target) return &n->child[i];
                break;
            }
            case NodeType256: {
                auto* n = static_cast<Node256*>(node);
                for (int i = 0; i < 256; i++)
                    if (n->child[i] == target) return &n->child[i];
                break;
            }
        }
        return nullptr;
    }

   public:
    // Hook: an inner node was replaced by a larger node type during expansion.
    // Updates fp and fp_prev tracking for any slot pointing at oldNode.
    // Default implementation handles the single-fp case.
    virtual void onNodeReplaced(ArtNode* oldNode, ArtNode* newNode,
                                ArtNode** newNodeRef) {
        if (fp == oldNode) {
            fp     = newNode;
            fp_ref = newNodeRef;
        } else if (fp_prev == oldNode) {
            fp_prev = newNode;
            ArtNode** ref = findChildPtr(newNode, fp);
            if (ref) fp_ref = ref;
        }
    }

    // Hook: a new parent node was spliced above fp (prefix expansion);
    // the child cell that holds fp has moved to a new address.
    virtual void onFpRefUpdate(ArtNode* targetFp, ArtNode** newRef) {
        if (fp == targetFp) fp_ref = newRef;
    }

    // Hook: the leaf tracked by fp_leaf was expanded into a Node4.
    // Adjusts fp_depth and redirects fp/fp_ref/fp_prev to the new Node4.
    virtual void onLeafExpanded(ArtNode* oldLeaf, Node4* newNode,
                                ArtNode** nodeRef, ArtNode* prevNode) {
        if (fp_leaf == oldLeaf) {
            if (!isLeaf(fp)) {
                fp_depth += fp->prefixLength;
                fp_depth++;
            }
            fp      = newNode;
            fp_ref  = nodeRef;
            fp_prev = prevNode;
        }
    }

    // Hook: a prefix mismatch spliced a new Node4 above fpNode.
    // Updates fp_prev and fp_depth for the slot that was tracking fpNode.
    virtual void onPrefixMismatch(ArtNode* fpNode, Node4* newParent,
                                  unsigned mismatchPrefixLen) {
        if (fp == fpNode) {
            fp_prev  = newParent;
            fp_depth += mismatchPrefixLen + 1;
        }
    }

    // Hook: a sorted insertion into `node` used memmove to shift existing
    // children, potentially invalidating any fp_ref that pointed into the
    // shifted range.  Implementors should re-derive fp_ref for every slot
    // where fp_prev == node using findChildPtr(node, fp).
    virtual void onParentShifted(ArtNode* node) {
        if (fp_prev == node) {
            ArtNode** ref = findChildPtr(node, fp);
            if (ref) fp_ref = ref;
        }
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

    // Helper function to compress the tree, called recursively using DFS
    void compressTreeHelper(ArtNode* node, ArtNode** nodeRef, int& compressed_nodes, int depth) {
        if (!node || isLeaf(node) || depth == 4) return;
        
        // First, recursively compress children
        switch (node->type) {
            case NodeType4: {
                Node4* n = static_cast<Node4*>(node);
                for (unsigned i = 0; i < n->count; i++) {
                    compressTreeHelper(n->child[i], &n->child[i], compressed_nodes, depth + 1);
                }
                break;
            }
            case NodeType16: {
                Node16* n = static_cast<Node16*>(node);
                for (unsigned i = 0; i < n->count; i++) {
                    compressTreeHelper(n->child[i], &n->child[i], compressed_nodes, depth + 1);
                }
                break;
            }
            case NodeType48: {
                Node48* n = static_cast<Node48*>(node);
                for (unsigned i = 0; i < 256; i++) {
                    if (n->childIndex[i] != emptyMarker) {
                        compressTreeHelper(n->child[n->childIndex[i]], 
                                        &n->child[n->childIndex[i]], 
                                        compressed_nodes, depth + 1);
                    }
                }
                break;
            }
            case NodeType256: {
                Node256* n = static_cast<Node256*>(node);
                for (unsigned i = 0; i < 256; i++) {
                    if (n->child[i]) {
                        compressTreeHelper(n->child[i], &n->child[i], compressed_nodes, depth + 1);
                    }
                }
                break;
            }
        }
        
        // After compressing children, check if this node has count == 1
        if (node->count == 1) {
            // Find the single child
            ArtNode* single_child = nullptr;
            uint8_t child_key = 0;
            
            switch (node->type) {
                case NodeType4: {
                    Node4* n = static_cast<Node4*>(node);
                    single_child = n->child[0];
                    child_key = n->key[0];
                    break;
                }
                case NodeType16: {
                    Node16* n = static_cast<Node16*>(node);
                    single_child = n->child[0];
                    child_key = n->key[0];
                    break;
                }
                case NodeType48: {
                    Node48* n = static_cast<Node48*>(node);
                    for (unsigned i = 0; i < 256; i++) {
                        if (n->childIndex[i] != emptyMarker) {
                            single_child = n->child[n->childIndex[i]];
                            child_key = i;
                            break;
                        }
                    }
                    break;
                }
                case NodeType256: {
                    Node256* n = static_cast<Node256*>(node);
                    for (unsigned i = 0; i < 256; i++) {
                        if (n->child[i]) {
                            single_child = n->child[i];
                            child_key = i;
                            break;
                        }
                    }
                    break;
                }
            }
            
            // Only compress if the child is not a leaf (to preserve tree structure)
            if (single_child && !isLeaf(single_child)) {
                // Combine prefixes: current node's prefix + child_key + child's prefix
                uint8_t combined_prefix[maxPrefixLength];
                unsigned combined_length = 0;
                
                // Copy current node's prefix
                unsigned copy_len = min(node->prefixLength, maxPrefixLength);
                memcpy(combined_prefix, node->prefix, copy_len);
                combined_length += copy_len;
                
                // Add the child key byte
                if (combined_length < maxPrefixLength) {
                    combined_prefix[combined_length++] = child_key;
                }
                
                // Add child's prefix
                if (combined_length < maxPrefixLength) {
                    unsigned child_prefix_len = min(single_child->prefixLength, maxPrefixLength - combined_length);
                    memcpy(combined_prefix + combined_length, single_child->prefix, child_prefix_len);
                    combined_length += child_prefix_len;
                }
                
                // Update child's prefix
                single_child->prefixLength = node->prefixLength + 1 + single_child->prefixLength;
                memcpy(single_child->prefix, combined_prefix, min(combined_length, maxPrefixLength));
                
                // Replace current node with the child
                *nodeRef = single_child;
                delete node;
                compressed_nodes++;
            }
        }
    }

};

}  // namespace ART