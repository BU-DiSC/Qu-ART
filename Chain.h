/*
 * Chain.h
 */

 #pragma once

 #include <assert.h>
 #include <emmintrin.h>  // x86 SSE intrinsics
 #include <immintrin.h>  // AVX512

 #include "ArtNode.h" // ArtNode definitions
 
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
    class ChainItem {
        public:
            ChainItem(ArtNode *node = nullptr): node_(node), next_(nullptr){}
            
            bool isNull() {
                return node_ == nullptr;
            }
            ChainItem*& next() {
                return next_;
            }
            ArtNode* nodeptr(){
                return node_;
            }
        private:
            ArtNode* node_;
            ChainItem *next_;
        };
        
        
        class ChainItemWithDepth: public ChainItem {
        public:
            ChainItemWithDepth(ArtNode *node = nullptr, int depth = 0, bool lequ = 0, bool hequ = 0): 
                ChainItem(node), depth_(depth), lequ_(lequ), hequ_(hequ){
            }
        public:
            int depth_ = 0;
            bool lequ_ = 0, hequ_ = 0;
        };
        
        class Chain {
        public:
            Chain(ChainItem *item = nullptr) {
                head_ = tail_ = item;
                if(item == nullptr || item->isNull()) length_ = 0, head_ = tail_ = nullptr;
                else length_ = 1;
            }
            void extend(std::unique_ptr<Chain> v) {
                Chain *extChain = v.get();
                if(extChain->length_ == 0) return;
                if(tail_ != nullptr)
                    tail_->next() = extChain->head_;
                else
                    head_ = extChain->head_;
                tail_ = extChain->tail_;
                length_ += extChain->length_;
            }
            void extend_item(ChainItem *item) {
                if(item == nullptr || item->isNull()) return;
                if(tail_ != nullptr)
                    tail_->next() = item;
                else
                    head_ = item;
                tail_ = item;
                length_ ++;
            }
        
            bool isEmpty() {
                return length_ == 0;
            }
            ChainItem* pop_front() {
                if(length_ == 0) return nullptr;
                else {
                    ChainItem *item = head_;
                    head_ = item->next();
                    item->next() = nullptr;
                    length_--;
                    if(length_ == 0) tail_ = head_ = nullptr;
                    return item;
                }
            }
        private:
            ChainItem *head_, *tail_;
            int length_ = 0;
        };        
}