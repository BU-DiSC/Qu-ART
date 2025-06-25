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

    void insert(uint8_t key[], uintptr_t value) {
        std::cout << "Value inserted: " << value << std::endl;
        if (value >= 261) {
            std::cout << "";
            printTree();
        }
        // First, check if the new key fits on the fast path.
        if (fp != NULL) {
            auto p =
                prefixMismatch(fp, key, fp_path_length - 1, maxPrefixLength);
            std::cout << "";
        }
        bool onFastPath =
            fp != NULL && prefixMismatch(fp, key, fp_path_length - 1,
                                         maxPrefixLength) == fp->prefixLength;
        if (onFastPath) {  // If so, insert from the end of the fast path.
            // fp_path_length--;  // decrement path length by one to avoid
            //  double-counting the fp node on the path
            std::cout << "Inserting to fast path" << std::endl;
            insert(this, fp, &fp, key, fp_path_length - 1, value,
                   maxPrefixLength);
        } else {  // Else, reset the fast path and insert from the root
            fp = NULL;
            fp_path_length = 0;
            fp_path.fill(NULL);
            std::cout << "Inserting to root" << std::endl;
            insert(this, root, &root, key, 0, value, maxPrefixLength);
        }
    }

    ArtNode* lookup(uint8_t key[]) {
        return lookup(root, key, maxPrefixLength, 0, maxPrefixLength);
    }

    Chain* rangelookup(uint8_t l_key[], unsigned l_keyLength, uint8_t h_key[],
                       uint8_t h_keyLength, unsigned depth,
                       unsigned maxKeyLength) {
        return rangelookup(this, root, l_key, l_keyLength, h_key, h_keyLength,
                           depth, maxKeyLength);
    }

   private:
    // Void insert function
    void insert(QuART_lil* tree, ArtNode* node, ArtNode** nodeRef,
                uint8_t key[], unsigned depth, uintptr_t value,
                unsigned maxKeyLength) {
        // Insert the leaf value into the tree

        // If tree is empty, populate with a single node.
        // Do not alter fp values; fp should only refer to internal nodes, not
        // leaf nodes
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

            // Swap out node for newNode in child list of parent node
            // if (fp_path_length > 0) {
            //     ArtNode** child =
            //         findChild(fp, node->prefix[fp_path_length - 1]);
            //     *child = newNode;
            // }
            // update the fast path
            fp = newNode;
            fp_path[fp_path_length++] = newNode;
            fp_leaf = newLeaf;

            if (fp_path_length == 4) {
                std::cout << "";
            }

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
                fp_path[fp_path_length++] = newNode;
                fp_leaf = newLeaf;
                // Swap out node for newNode in child list of parent node
                // ArtNode** child =
                //     findChild(fp, node->prefix[fp_path_length - 1]);
                // *child = newNode;

                if (fp_path_length == 4) {
                    std::cout << "";
                }

                return;
            }
            depth += node->prefixLength;
        }
        std::cout << "Before: " << fp_path_length << std::endl;
        // using the condition avoids double-counting the end of a fp when
        // inserting to a fp
        if (node != fp_path[fp_path_length - 1]) {
            fp_path[fp_path_length++] = node;
            fp = node;
        }
        std::cout << "After: " << fp_path_length << std::endl;

        if (fp_path_length == 4) {
            std::cout << "";
        }

        // Recurse
        ArtNode** child = findChild(node, key[depth]);
        if (*child) {
            insert(tree, *child, child, key, depth + 1, value, maxKeyLength);
            return;
        }

        // Insert leaf into inner node
        ArtNode* newNode = makeLeaf(value);
        fp_leaf = newNode;
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
    Chain* rangelookup(QuART_lil* tree, ArtNode* node, uint8_t l_key[],
                       unsigned l_keyLength, uint8_t h_key[],
                       uint8_t h_keyLength, unsigned depth,
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