/*
 * Chain.h
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
#include <cassert>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <locale>
#include <memory>
#include <stdexcept>
#include <vector>

#include "ArtNode.h"  // ArtNode definitions

namespace ART {
class ChainItem {
   public:
    ChainItem(ArtNode *node = nullptr) : node_(node), next_(nullptr) {}

    bool isNull() { return node_ == nullptr; }
    ChainItem *&next() { return next_; }
    ArtNode *nodeptr() { return node_; }

   private:
    ArtNode *node_;
    ChainItem *next_;
};

class ChainItemWithDepth : public ChainItem {
   public:
    ChainItemWithDepth(ArtNode *node = nullptr, int depth = 0, bool lequ = 0,
                       bool hequ = 0)
        : ChainItem(node), depth_(depth), lequ_(lequ), hequ_(hequ) {}

   public:
    int depth_ = 0;
    bool lequ_ = 0, hequ_ = 0;
};

class Chain {
   public:
    Chain(ChainItem *item = nullptr) {
        head_ = tail_ = item;
        if (item == nullptr || item->isNull())
            length_ = 0, head_ = tail_ = nullptr;
        else
            length_ = 1;
    }
    void extend(std::unique_ptr<Chain> v) {
        Chain *extChain = v.get();
        if (extChain->length_ == 0) return;
        if (tail_ != nullptr)
            tail_->next() = extChain->head_;
        else
            head_ = extChain->head_;
        tail_ = extChain->tail_;
        length_ += extChain->length_;
    }
    void extend_item(ChainItem *item) {
        if (item == nullptr || item->isNull()) return;
        if (tail_ != nullptr)
            tail_->next() = item;
        else
            head_ = item;
        tail_ = item;
        length_++;
    }

    bool isEmpty() { return length_ == 0; }
    ChainItem *pop_front() {
        if (length_ == 0)
            return nullptr;
        else {
            ChainItem *item = head_;
            head_ = item->next();
            item->next() = nullptr;
            length_--;
            if (length_ == 0) tail_ = head_ = nullptr;
            return item;
        }
    }

    Chain *findChildbyRange(ArtNode *n, uint8_t lkeyByte, uint8_t hkeyByte,
                            int depth, bool lequ, bool hequ) {
        // Find the next child for the keyByte
        Chain *ret = new Chain();
        switch (n->type) {
            case NodeType4: {
                Node4 *node = static_cast<Node4 *>(n);
                for (unsigned i = 0; i < node->count; i++)
                    if (node->key[i] >= lkeyByte && node->key[i] <= hkeyByte)
                        ret->extend_item((ChainItem *)new ChainItemWithDepth(
                            node->child[i], depth + 1,
                            (node->key[i] == lkeyByte) & lequ,
                            (node->key[i] == hkeyByte) * hequ));
            } break;
            case NodeType16: {
                Node16 *node = static_cast<Node16 *>(n);
                __m128i ld =
                    _mm_loadu_si128(reinterpret_cast<__m128i *>(node->key));
                __m128i cmp = _mm_or_si128(
                    _mm_cmplt_epi8(
                        ld, _mm_set1_epi8(flipSign(lkeyByte))), /* ld < l */
                    _mm_cmplt_epi8(_mm_set1_epi8(flipSign(hkeyByte)),
                                   ld) /* r > ld */
                );
                unsigned bitfield =
                    (~_mm_movemask_epi8(cmp)) & ((1 << node->count) - 1);
                // uint32_t cmp =
                //     _mm_cmple_epi8_mask(_mm_set1_epi8(flipSign(lkeyByte)),
                //     ld) &
                //     _mm_cmple_epi8_mask(_mm_set1_epi8(flipSign(hkeyByte)),
                //     ld);
                // uint32_t bitfield = cmp & ((1 << node->count) - 1);
                if (bitfield) {
                    int idx = ctz(bitfield);
                    ret->extend_item((ChainItem *)new ChainItemWithDepth(
                        node->child[idx], depth + 1, lequ,
                        (bitfield == 0) & hequ));
                    bitfield = bitfield & (bitfield - 1);
                }
                while (bitfield) {
                    int idx = ctz(bitfield);
                    ret->extend_item((ChainItem *)new ChainItemWithDepth(
                        node->child[idx], depth + 1, false,
                        (bitfield == 0) & hequ));
                    bitfield = bitfield & (bitfield - 1);
                }
            } break;
            case NodeType48: {
                Node48 *node = static_cast<Node48 *>(n);
                for (uint16_t keyByte = lkeyByte; keyByte <= hkeyByte;
                     keyByte++)
                    if (node->childIndex[keyByte] != emptyMarker)
                        ret->extend_item((ChainItem *)new ChainItemWithDepth(
                            node->child[node->childIndex[keyByte]], depth + 1,
                            (keyByte == lkeyByte) & lequ,
                            (keyByte == hkeyByte) & hequ));
                    else
                        continue;
                // TODO: optimize this part
            } break;
            case NodeType256: {
                Node256 *node = static_cast<Node256 *>(n);
                for (uint16_t keyByte = lkeyByte; keyByte <= hkeyByte;
                     keyByte++) {
                    ret->extend_item((ChainItem *)new ChainItemWithDepth(
                        node->child[keyByte], depth + 1,
                        (keyByte == lkeyByte) & lequ,
                        (keyByte == hkeyByte) & hequ));
                }
            } break;
        }
        return ret;
    }

   private:
    ChainItem *head_, *tail_;
    int length_ = 0;
};
}  // namespace ART