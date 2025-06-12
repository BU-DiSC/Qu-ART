/*
 * Helper.h
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

#include "ArtNode.h" // ArtNode definitions
#include "Chain.h" // Chain definitions
 
#include <algorithm>  // std::random_shuffle
#include <chrono>
 
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <locale>
#include <memory>
#include <stdexcept>
 
#include <vector>

namespace ART {
    class ART;

    inline ArtNode* makeLeaf(uintptr_t tid) {
        // Create a pseudo-leaf
        return reinterpret_cast<ArtNode*>((tid << 1) | 1);
    }
    
    inline uintptr_t getLeafValue(ArtNode* node) {
        // The the value stored in the pseudo-leaf
        return reinterpret_cast<uintptr_t>(node) >> 1;
    }
    
    inline bool isLeaf(ArtNode* node) {
        // Is the node a leaf?
        return reinterpret_cast<uintptr_t>(node) & 1;
    }
    
    uint8_t flipSign(uint8_t keyByte) {
        // Flip the sign bit, enables signed SSE comparison of unsigned values, used
        // by Node16
        return keyByte ^ 128;
    }
    
    void loadKey(uintptr_t tid, uint8_t key[]) {
        // Store the key of the tuple into the key vector
        // Implementation is database specific
        reinterpret_cast<uint64_t*>(key)[0] = __builtin_bswap64(tid);
    }
    
    // This address is used to communicate that search failed
    ArtNode* nullNode = NULL;
    
    static inline unsigned ctz(uint16_t x) {
        // Count trailing zeros, only defined for x>0
    #ifdef __GNUC__
        return __builtin_ctz(x);
    #else
        // Adapted from Hacker's Delight
        unsigned n = 1;
        if ((x & 0xFF) == 0) {
            n += 8;
            x = x >> 8;
        }
        if ((x & 0x0F) == 0) {
            n += 4;
            x = x >> 4;
        }
        if ((x & 0x03) == 0) {
            n += 2;
            x = x >> 2;
        }
        return n - (x & 1);
    #endif
    }
    
    
    
    
    ArtNode** findChild(ArtNode* n, uint8_t keyByte) {
        // Find the next child for the keyByte
        switch (n->type) {
            case NodeType4: {
                Node4* node = static_cast<Node4*>(n);
                for (unsigned i = 0; i < node->count; i++)
                    if (node->key[i] == keyByte) return &node->child[i];
                return &nullNode;
            }
            case NodeType16: {
                Node16* node = static_cast<Node16*>(n);
                __m128i cmp = _mm_cmpeq_epi8(
                    _mm_set1_epi8(flipSign(keyByte)),
                    _mm_loadu_si128(reinterpret_cast<__m128i*>(node->key)));
                unsigned bitfield =
                    _mm_movemask_epi8(cmp) & ((1 << node->count) - 1);
                if (bitfield)
                    return &node->child[ctz(bitfield)];
                else
                    return &nullNode;
            }
            case NodeType48: {
                Node48* node = static_cast<Node48*>(n);
                if (node->childIndex[keyByte] != emptyMarker)
                    return &node->child[node->childIndex[keyByte]];
                else
                    return &nullNode;
            }
            case NodeType256: {
                Node256* node = static_cast<Node256*>(n);
                return &(node->child[keyByte]);
            }
        }
        throw;  // Unreachable
    }
    
    
    Chain* findChildbyRange(ArtNode* n, uint8_t lkeyByte, uint8_t hkeyByte, int depth, bool lequ, bool hequ) {
        // Find the next child for the keyByte
        Chain* ret = new Chain();
        switch (n->type) {
            case NodeType4: {
                Node4* node = static_cast<Node4*>(n);
                for (unsigned i = 0; i < node->count; i++)
                    if (node->key[i] >= lkeyByte && node->key[i] <= hkeyByte) 
                        ret->extend_item((ChainItem *)new ChainItemWithDepth(node->child[i], 
                                                                             depth + 1, 
                                                                             (node->key[i] == lkeyByte) & lequ, 
                                                                             (node->key[i] == hkeyByte) * hequ));
            } break;
            case NodeType16: {
                Node16* node = static_cast<Node16*>(n);
                __m128i ld = _mm_loadu_si128(reinterpret_cast<__m128i*>(node->key));
                __m128i cmp = _mm_or_si128(
                    _mm_cmplt_epi8(ld, _mm_set1_epi8(flipSign(lkeyByte))), /* ld < l */
                    _mm_cmplt_epi8(_mm_set1_epi8(flipSign(hkeyByte)),ld)/* r > ld */
                );
                unsigned bitfield = (~_mm_movemask_epi8(cmp)) & ((1 << node->count) - 1);
                // uint32_t cmp = 
                //     _mm_cmple_epi8_mask(_mm_set1_epi8(flipSign(lkeyByte)), ld)
                //     & _mm_cmple_epi8_mask(_mm_set1_epi8(flipSign(hkeyByte)), ld);
                // uint32_t bitfield = cmp & ((1 << node->count) - 1);
                if(bitfield) {
                    int idx = ctz(bitfield);
                    ret->extend_item((ChainItem *)new ChainItemWithDepth(node->child[idx], 
                                                                         depth + 1, 
                                                                         lequ, 
                                                                         (bitfield == 0) & hequ));
                    bitfield = bitfield & (bitfield - 1);
                }
                while(bitfield) {
                    int idx = ctz(bitfield);
                    ret->extend_item((ChainItem *)new ChainItemWithDepth(node->child[idx], 
                                                                         depth + 1, 
                                                                         false, 
                                                                         (bitfield == 0) & hequ));
                    bitfield = bitfield & (bitfield - 1);
                }
            } break;
            case NodeType48: {
                Node48* node = static_cast<Node48*>(n);
                for(uint16_t keyByte = lkeyByte; keyByte <= hkeyByte; keyByte ++)
                    if (node->childIndex[keyByte] != emptyMarker)
                        ret->extend_item((ChainItem *)new ChainItemWithDepth(node->child[node->childIndex[keyByte]], 
                                                                             depth + 1, 
                                                                             (keyByte == lkeyByte) & lequ, 
                                                                             (keyByte == hkeyByte) & hequ));
                    else
                        continue;
                // TODO: optimize this part
            } break;
            case NodeType256: {
                Node256* node = static_cast<Node256*>(n);
                for(uint16_t keyByte = lkeyByte; keyByte <= hkeyByte; keyByte ++) {
                    ret->extend_item((ChainItem *)new ChainItemWithDepth(node->child[keyByte],
                                                                         depth + 1, 
                                                                         (keyByte == lkeyByte) & lequ, 
                                                                         (keyByte == hkeyByte) & hequ));
                }
            } break;
        }
        return ret;  
    }
    
    ArtNode* minimum(ArtNode* node) {
        // Find the leaf with smallest key
        if (!node) return NULL;
    
        if (isLeaf(node)) return node;
    
        switch (node->type) {
            case NodeType4: {
                Node4* n = static_cast<Node4*>(node);
                return minimum(n->child[0]);
            }
            case NodeType16: {
                Node16* n = static_cast<Node16*>(node);
                return minimum(n->child[0]);
            }
            case NodeType48: {
                Node48* n = static_cast<Node48*>(node);
                unsigned pos = 0;
                while (n->childIndex[pos] == emptyMarker) pos++;
                return minimum(n->child[n->childIndex[pos]]);
            }
            case NodeType256: {
                Node256* n = static_cast<Node256*>(node);
                unsigned pos = 0;
                while (!n->child[pos]) pos++;
                return minimum(n->child[pos]);
            }
        }
        throw;  // Unreachable
    }
    
    ArtNode* maximum(ArtNode* node) {
        // Find the leaf with largest key
        if (!node) return NULL;
    
        if (isLeaf(node)) return node;
    
        switch (node->type) {
            case NodeType4: {
                Node4* n = static_cast<Node4*>(node);
                return maximum(n->child[n->count - 1]);
            }
            case NodeType16: {
                Node16* n = static_cast<Node16*>(node);
                return maximum(n->child[n->count - 1]);
            }
            case NodeType48: {
                Node48* n = static_cast<Node48*>(node);
                unsigned pos = 255;
                while (n->childIndex[pos] == emptyMarker) pos--;
                return maximum(n->child[n->childIndex[pos]]);
            }
            case NodeType256: {
                Node256* n = static_cast<Node256*>(node);
                unsigned pos = 255;
                while (!n->child[pos]) pos--;
                return maximum(n->child[pos]);
            }
        }
        throw;  // Unreachable
    }
    
    bool leafMatches(ArtNode* leaf, uint8_t key[], unsigned keyLength,
                     unsigned depth, unsigned maxKeyLength) {
        // Check if the key of the leaf is equal to the searched key
        if (depth != keyLength) {
            uint8_t leafKey[maxKeyLength];
            loadKey(getLeafValue(leaf), leafKey);
            for (unsigned i = depth; i < keyLength; i++)
                if (leafKey[i] != key[i]) return false;
        }
        return true;
    }
    
    unsigned prefixMismatch(ArtNode* node, uint8_t key[], unsigned depth,
                            unsigned maxKeyLength) {
        // Compare the key with the prefix of the node, return the number matching
        // bytes
        unsigned pos;
        if (node->prefixLength > maxPrefixLength) {
            for (pos = 0; pos < maxPrefixLength; pos++)
                if (key[depth + pos] != node->prefix[pos]) return pos;
            uint8_t minKey[maxKeyLength];
            loadKey(getLeafValue(minimum(node)), minKey);
            for (; pos < node->prefixLength; pos++)
                if (key[depth + pos] != minKey[depth + pos]) return pos;
        } else {
            for (pos = 0; pos < node->prefixLength; pos++)
                if (key[depth + pos] != node->prefix[pos]) return pos;
        }
        return pos;
    }
        
    
    
    ArtNode* lookupPessimistic(ArtNode* node, uint8_t key[], unsigned keyLength,
                               unsigned depth, unsigned maxKeyLength) {
        // Find the node with a matching key, alternative pessimistic version
    
        while (node != NULL) {
            if (isLeaf(node)) {
                if (leafMatches(node, key, keyLength, depth, maxKeyLength))
                    return node;
                return NULL;
            }
    
            if (prefixMismatch(node, key, depth, maxKeyLength) !=
                node->prefixLength)
                return NULL;
            else
                depth += node->prefixLength;
    
            node = *findChild(node, key[depth]);
            depth++;
        }
    
        return NULL;
    }    
    unsigned min(unsigned a, unsigned b) {
        // Helper function
        return (a < b) ? a : b;
    }
    
    void copyPrefix(ArtNode* src, ArtNode* dst) {
        // Helper function that copies the prefix from the source to the destination
        // node
        dst->prefixLength = src->prefixLength;
        memcpy(dst->prefix, src->prefix, min(src->prefixLength, maxPrefixLength));
    }

    void insertNode256(ART* tree, Node256* node, ArtNode** nodeRef, uint8_t keyByte,
        ArtNode* child) {
        // Insert leaf into inner node
        node->count++;
        node->child[keyByte] = child;
    }

    void insertNode48(ART* tree, Node48* node, ArtNode** nodeRef, uint8_t keyByte, ArtNode* child) {
        // Insert leaf into inner node
        if (node->count < 48) {
            // Insert element
            unsigned pos = node->count;
            if (node->child[pos])
                for (pos = 0; node->child[pos] != NULL; pos++);
                    node->child[pos] = child;
                    node->childIndex[keyByte] = pos;
                    node->count++;
        } else {
            // Grow to Node256
            Node256* newNode = new Node256();
            for (unsigned i = 0; i < 256; i++)
            if (node->childIndex[i] != 48)
                newNode->child[i] = node->child[node->childIndex[i]];
            newNode->count = node->count;
            copyPrefix(node, newNode);
            *nodeRef = newNode;
            delete node;
            return insertNode256(tree, newNode, nodeRef, keyByte, child);
        }
    }

    void insertNode16(ART* tree, Node16* node, ArtNode** nodeRef, uint8_t keyByte,
        ArtNode* child) {
        // Insert leaf into inner node
        if (node->count < 16) {
        // Insert element
        uint8_t keyByteFlipped = flipSign(keyByte);
        __m128i cmp = _mm_cmplt_epi8(
        _mm_set1_epi8(keyByteFlipped),
        _mm_loadu_si128(reinterpret_cast<__m128i*>(node->key)));
        uint16_t bitfield =
        _mm_movemask_epi8(cmp) & (0xFFFF >> (16 - node->count));
        unsigned pos = bitfield ? ctz(bitfield) : node->count;
        memmove(node->key + pos + 1, node->key + pos, node->count - pos);
        memmove(node->child + pos + 1, node->child + pos,
            (node->count - pos) * sizeof(uintptr_t));
        node->key[pos] = keyByteFlipped;
        node->child[pos] = child;
        node->count++;
        } else {
        // Grow to Node48
        Node48* newNode = new Node48();
        *nodeRef = newNode;
        memcpy(newNode->child, node->child, node->count * sizeof(uintptr_t));
        for (unsigned i = 0; i < node->count; i++)
            newNode->childIndex[flipSign(node->key[i])] = i;
        copyPrefix(node, newNode);
        newNode->count = node->count;
        delete node;
        return insertNode48(tree, newNode, nodeRef, keyByte, child);
        }
    }



    void insertNode4(ART* tree, Node4* node, ArtNode** nodeRef, uint8_t keyByte,
                    ArtNode* child) {
        // Insert leaf into inner node
        if (node->count < 4) {
            // Insert element
            unsigned pos;
            for (pos = 0; (pos < node->count) && (node->key[pos] < keyByte); pos++);
            memmove(node->key + pos + 1, node->key + pos, node->count - pos);
            memmove(node->child + pos + 1, node->child + pos,
                    (node->count - pos) * sizeof(uintptr_t));
            node->key[pos] = keyByte;
            node->child[pos] = child;
            node->count++;
        } else {
            // Grow to Node16
            Node16* newNode = new Node16();
            *nodeRef = newNode;
            newNode->count = 4;
            copyPrefix(node, newNode);
            for (unsigned i = 0; i < 4; i++)
                newNode->key[i] = flipSign(node->key[i]);
            memcpy(newNode->child, node->child, node->count * sizeof(uintptr_t));
            delete node;
            return insertNode16(tree, newNode, nodeRef, keyByte, child);
        }
    }


        
    void eraseNode4(ART* tree, Node4* node, ArtNode** nodeRef, ArtNode** leafPlace) {
        // Delete leaf from inner node
        unsigned pos = leafPlace - node->child;
        memmove(node->key + pos, node->key + pos + 1, node->count - pos - 1);
        memmove(node->child + pos, node->child + pos + 1,
                (node->count - pos - 1) * sizeof(uintptr_t));
        node->count--;

        if (node->count == 1) {
            // Get rid of one-way node
            ArtNode* child = node->child[0];
            if (!isLeaf(child)) {
                // Concantenate prefixes
                unsigned l1 = node->prefixLength;
                if (l1 < maxPrefixLength) {
                    node->prefix[l1] = node->key[0];
                    l1++;
                }
                if (l1 < maxPrefixLength) {
                    unsigned l2 = min(child->prefixLength, maxPrefixLength - l1);
                    memcpy(node->prefix + l1, child->prefix, l2);
                    l1 += l2;
                }
                // Store concantenated prefix
                memcpy(child->prefix, node->prefix, min(l1, maxPrefixLength));
                child->prefixLength += node->prefixLength + 1;
            }
            *nodeRef = child;
            delete node;
        }
    }

    void eraseNode16(ART* tree, Node16* node, ArtNode** nodeRef, ArtNode** leafPlace) {
        // Delete leaf from inner node
        unsigned pos = leafPlace - node->child;
        memmove(node->key + pos, node->key + pos + 1, node->count - pos - 1);
        memmove(node->child + pos, node->child + pos + 1,
                (node->count - pos - 1) * sizeof(uintptr_t));
        node->count--;

        if (node->count == 3) {
            // Shrink to Node4
            Node4* newNode = new Node4();
            newNode->count = node->count;
            copyPrefix(node, newNode);
            for (unsigned i = 0; i < 4; i++)
                newNode->key[i] = flipSign(node->key[i]);
            memcpy(newNode->child, node->child, sizeof(uintptr_t) * 4);
            *nodeRef = newNode;
            delete node;
        }
    }
    
    void eraseNode48(ART* tree, Node48* node, ArtNode** nodeRef, uint8_t keyByte) {
        // Delete leaf from inner node
        node->child[node->childIndex[keyByte]] = NULL;
        node->childIndex[keyByte] = emptyMarker;
        node->count--;

        if (node->count == 12) {
            // Shrink to Node16
            Node16* newNode = new Node16();
            *nodeRef = newNode;
            copyPrefix(node, newNode);
            for (unsigned b = 0; b < 256; b++) {
                if (node->childIndex[b] != emptyMarker) {
                    newNode->key[newNode->count] = flipSign(b);
                    newNode->child[newNode->count] =
                        node->child[node->childIndex[b]];
                    newNode->count++;
                }
            }
            delete node;
        }
    }

    void eraseNode256(ART* tree, Node256* node, ArtNode** nodeRef, uint8_t keyByte) {
        // Delete leaf from inner node
        node->child[keyByte] = NULL;
        node->count--;

        if (node->count == 37) {
            // Shrink to Node48
            Node48* newNode = new Node48();
            *nodeRef = newNode;
            copyPrefix(node, newNode);
            for (unsigned b = 0; b < 256; b++) {
                if (node->child[b]) {
                    newNode->childIndex[b] = newNode->count;
                    newNode->child[newNode->count] = node->child[b];
                    newNode->count++;
                }
            }
            delete node;
        }
    }    

}