/*
 * ArtNode and its derived classes for an Adaptive Radix Tree (ART) implementation
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

    // Constants for the node types
    static const int8_t NodeType4 = 0;
    static const int8_t NodeType16 = 1;
    static const int8_t NodeType48 = 2;
    static const int8_t NodeType256 = 3;

    // The maximum prefix length for compressed paths stored in the
    // header, if the path is longer it is loaded from the database on
    // demand
    static const unsigned maxPrefixLength = 9;

    // Shared header of all inner nodes
    struct ArtNode {
        // length of the compressed path (prefix)
        uint32_t prefixLength;
        // number of non-null children
        uint16_t count;
        // node type
        int8_t type;
        // compressed path (prefix)
        uint8_t prefix[maxPrefixLength];

        ArtNode(int8_t type) : prefixLength(0), count(0), type(type) {}
    };

    // Node with up to 4 children
    struct Node4 : ArtNode {
        uint8_t key[4];
        ArtNode* child[4];

        Node4() : ArtNode(NodeType4) {
            memset(key, 0, sizeof(key));
            memset(child, 0, sizeof(child));
        }
    };

    // Node with up to 16 children
    struct Node16 : ArtNode {
        uint8_t key[16];
        ArtNode* child[16];

        Node16() : ArtNode(NodeType16) {
            memset(key, 0, sizeof(key));
            memset(child, 0, sizeof(child));
        }
    };

    static const uint8_t emptyMarker = 48;

    // Node with up to 48 children
    struct Node48 : ArtNode {
        uint8_t childIndex[256];
        ArtNode* child[48];

        Node48() : ArtNode(NodeType48) {
            memset(childIndex, emptyMarker, sizeof(childIndex));
            memset(child, 0, sizeof(child));
        }
    };

    // Node with up to 256 children
    struct Node256 : ArtNode {
        ArtNode* child[256];

        Node256() : ArtNode(NodeType256) { memset(child, 0, sizeof(child)); }
    };

}