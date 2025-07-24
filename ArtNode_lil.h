
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
#include <cassert>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <locale>
#include <memory>
#include <stdexcept>
#include <vector>

#include "ART.h"
#include "ArtNode.h"
#include "Helper.h"

namespace ART {

void Node4::insertNode4_lil(ART* tree, ArtNode** nodeRef, uint8_t keyByte,
                            ArtNode* child) {
    // Insert leaf into inner node
    if (this->count < 4) {
        // Insert element
        unsigned pos;
        for (pos = 0; (pos < this->count) && (this->key[pos] < keyByte); pos++);
        memmove(this->key + pos + 1, this->key + pos, this->count - pos);
        memmove(this->child + pos + 1, this->child + pos,
                (this->count - pos) * sizeof(uintptr_t));
        this->key[pos] = keyByte;
        this->child[pos] = child;
        this->count++;
    } else {
        // Grow to Node16
        Node16* newNode = new Node16();
        *nodeRef = newNode;

        // update fast path
        tree->fp = newNode;
        tree->fp_path[tree->fp_path_length - 1] = newNode;
        tree->fp_path_ref[tree->fp_path_length - 1] = nodeRef;

        newNode->count = 4;
        copyPrefix(this, newNode);
        for (unsigned i = 0; i < 4; i++)
            newNode->key[i] = flipSign(this->key[i]);
        memcpy(newNode->child, this->child, this->count * sizeof(uintptr_t));
        delete this;
        return newNode->insertNode16_lil(tree, nodeRef, keyByte, child);
    }
}

void Node16::insertNode16_lil(ART* tree, ArtNode** nodeRef, uint8_t keyByte,
                              ArtNode* child) {
    // Insert leaf into inner node
    if (this->count < 16) {
        // Insert element
        uint8_t keyByteFlipped = flipSign(keyByte);
        __m128i cmp = _mm_cmplt_epi8(
            _mm_set1_epi8(keyByteFlipped),
            _mm_loadu_si128(reinterpret_cast<__m128i*>(this->key)));
        uint16_t bitfield =
            _mm_movemask_epi8(cmp) & (0xFFFF >> (16 - this->count));
        unsigned pos = bitfield ? ctz(bitfield) : this->count;
        memmove(this->key + pos + 1, this->key + pos, this->count - pos);
        memmove(this->child + pos + 1, this->child + pos,
                (this->count - pos) * sizeof(uintptr_t));
        this->key[pos] = keyByteFlipped;
        this->child[pos] = child;
        this->count++;
    } else {
        // Grow to Node48
        Node48* newNode = new Node48();
        *nodeRef = newNode;

        // update fast path
        tree->fp = newNode;
        tree->fp_path[tree->fp_path_length - 1] = newNode;
        tree->fp_path_ref[tree->fp_path_length - 1] = nodeRef;

        memcpy(newNode->child, this->child, this->count * sizeof(uintptr_t));
        for (unsigned i = 0; i < this->count; i++)
            newNode->childIndex[flipSign(this->key[i])] = i;
        copyPrefix(this, newNode);
        newNode->count = this->count;
        delete this;
        return newNode->insertNode48_lil(tree, nodeRef, keyByte, child);
    }
}

void Node48::insertNode48_lil(ART* tree, ArtNode** nodeRef, uint8_t keyByte,
                              ArtNode* child) {
    // Insert leaf into inner node
    if (this->count < 48) {
        // Insert element
        unsigned pos = this->count;
        if (this->child[pos])
            for (pos = 0; this->child[pos] != NULL; pos++);
        this->child[pos] = child;
        this->childIndex[keyByte] = pos;
        this->count++;
    } else {
        // Grow to Node256
        Node256* newNode = new Node256();
        for (unsigned i = 0; i < 256; i++)
            if (this->childIndex[i] != 48)
                newNode->child[i] = this->child[this->childIndex[i]];
        newNode->count = this->count;
        copyPrefix(this, newNode);
        *nodeRef = newNode;

        // update fast path
        tree->fp = newNode;
        tree->fp_path[tree->fp_path_length - 1] = newNode;
        tree->fp_path_ref[tree->fp_path_length - 1] = nodeRef;

        delete this;
        return newNode->insertNode256_lil(tree, nodeRef, keyByte, child);
    }
}

// suppress warnings about unused parameters to ensure consistency
void Node256::insertNode256_lil(ART* tree [[maybe_unused]],
                                ArtNode** nodeRef [[maybe_unused]],
                                uint8_t keyByte, ArtNode* child) {
    // Insert leaf into inner node
    this->count++;
    this->child[keyByte] = child;
}
}  // namespace ART