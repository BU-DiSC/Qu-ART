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

#include "ART.h"          // Original ART class
#include "ArtNode_lil.h"  // ArtNode definitions
#include "Chain.h"        // Chain definitions
#include "Helper.h"       // Helper functions

namespace ART {

class QuART_lil : ART::ART {
   public:
    // constructor
    QuART_lil() : ART() {}

    bool canLilInsert(uint8_t key[]) {
        // if root is null or root is a leaf, we cannot lil insert
        if (this->root == NULL || isLeaf(this->root)) {
            return false;
        }
        int fpLeafValue = getLeafValue(this->fp_leaf);
        // convert int value in fp leaf into byte array
        std::array<uint8_t, maxPrefixLength> leafArr = intToArr(fpLeafValue);
        // use memcmp for fast comparison
        return memcmp(key, leafArr.data(), maxPrefixLength - 1) == 0;
    }

    void insert(uint8_t key[], uintptr_t value) {
        // std::cout << "Value inserted: " << value << std::endl;
        if (value >= 256) {
            std::cout << std::endl;
            printTree();
        }

        // Check if the fast path exists and if the new key fits on the fast
        // path.
        if (fp != NULL) {
            bool onFastPath = canLilInsert(key);
            bool isFull;
            switch (fp->type) {
                case NodeType4:
                    isFull = fp->count == 4;
                    break;
                case NodeType16:
                    isFull = fp->count == 16;
                    break;
                case NodeType48:
                    isFull = fp->count == 48;
                    break;
                case NodeType256:
                    isFull = fp->count == 256;
                    break;
            };
            if (onFastPath && !isFull) {
                // If so, insert from the end of the fast path.
                insertRecursive(this, fp, fp_ref, key, fp_depth, value,
                                maxPrefixLength, true);
                return;
            }
        }
        // Else, reset the fast path and insert from the root
        fp = root;
        fp_ref = &root;
        fp_path_length = 1;
        // fp_depth = (root != NULL && !isLeaf(root)) ? root->prefixLength : 0;
        fp_depth = 0;
        fp_path.fill(NULL);
        fp_path_ref.fill(NULL);
        fp_path[0] = root;
        fp_path_ref[0] = &root;
        fp_leaf = NULL;
        insertRecursive(this, root, &root, key, 0, value, maxPrefixLength,
                        true);
    }

    ArtNode* lookup(uint8_t key[]) {
        return lookup(root, key, maxPrefixLength, 0, maxPrefixLength);
    }

    Chain* rangelookup(uint8_t l_key[], unsigned l_keyLength, uint8_t h_key[],
                       uint8_t h_keyLength, unsigned maxKeyLength) {
        return rangelookup(root, l_key, l_keyLength, h_key, h_keyLength,
                           maxKeyLength);
    }

   private:
    // Void insert function
    void insertRecursive(QuART_lil* tree, ArtNode* node, ArtNode** nodeRef,
                         uint8_t key[], unsigned depth, uintptr_t value,
                         unsigned maxKeyLength, bool firstCall) {
        // Insert the leaf value into the tree

        // If tree is empty, populate with a single node.
        // Do not alter fp values; fp should only refer to internal nodes,
        // not leaf nodes
        if (node == NULL) {
            ArtNode* newLeaf = makeLeaf(value);
            *nodeRef = newLeaf;
            fp_leaf = newLeaf;
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
            ArtNode* newLeaf = makeLeaf(value);
            newNode->insertNode4(this, nodeRef, key[depth + newPrefixLength],
                                 newLeaf);

            // update the fast path
            fp = newNode;
            fp_ref = nodeRef;
            fp_path[fp_path_length] = newNode;
            fp_path_ref[fp_path_length] = nodeRef;
            fp_path_length++;
            // fp_path_length = 1;
            // fp_depth = newNode->prefixLength;
            fp_leaf = newLeaf;

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

                ArtNode* newLeaf = makeLeaf(value);
                newNode->insertNode4(this, nodeRef, key[depth + mismatchPos],
                                     newLeaf);

                // update the fast path
                fp = newNode;
                fp_ref = nodeRef;
                // fp_depth += fp_path[fp_path_length - 1]->prefixLength;
                fp_path[fp_path_length] = newNode;
                fp_path_ref[fp_path_length] = nodeRef;
                // fp_path_length++;
                //  fp_depth += newNode->prefixLength;
                fp_leaf = newLeaf;

                return;
            }
            depth += node->prefixLength;
        }
        // The first call in the recursive loop always starts on a node
        // already in the fast path. To avoid double-counting it, only
        // update these values on subsequent calls.
        if (!firstCall) {
            fp = node;
            fp_ref = nodeRef;
            // fp_depth += fp_path[fp_path_length - 1]->prefixLength;
            fp_path[fp_path_length] = node;
            fp_path_ref[fp_path_length] = nodeRef;
            fp_path_length++;
            // fp_depth += node->prefixLength;
        }

        // Recurse
        ArtNode** child = findChild(node, key[depth]);
        if (*child) {
            fp_depth += node->prefixLength;
            insertRecursive(tree, *child, child, key, depth + 1, value,
                            maxKeyLength, false);
            return;
        }

        // Insert leaf into inner node
        ArtNode* newLeaf = makeLeaf(value);
        fp_leaf = newLeaf;

        switch (node->type) {
            case NodeType4:
                static_cast<Node4*>(node)->insertNode4(this, nodeRef,
                                                       key[depth], newLeaf);
                break;
            case NodeType16:
                static_cast<Node16*>(node)->insertNode16(this, nodeRef,
                                                         key[depth], newLeaf);
                break;
            case NodeType48:
                static_cast<Node48*>(node)->insertNode48(this, nodeRef,
                                                         key[depth], newLeaf);
                break;
            case NodeType256:
                static_cast<Node256*>(node)->insertNode256(this, nodeRef,
                                                           key[depth], newLeaf);
                break;
        }
    }

    // Lookup function, returns ArtNode
    ArtNode* lookup(ArtNode* node, uint8_t key[], unsigned keyLength,
                    unsigned depth, unsigned maxKeyLength) {
        // Find the node with a matching key, optimistic version

        bool skippedPrefix = false;  // Did we optimistically skip some
                                     // prefix without checking it?

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
                    static_cast<Node4*>(node)->eraseNode4(nodeRef, child);
                    break;
                case NodeType16:
                    static_cast<Node16*>(node)->eraseNode16(nodeRef, child);
                    break;
                case NodeType48:
                    static_cast<Node48*>(node)->eraseNode48(nodeRef,
                                                            key[depth]);
                    break;
                case NodeType256:
                    static_cast<Node256*>(node)->eraseNode256(nodeRef,
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

   public:
    void printTree() { printTree(this->root, 0); }

   private:
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