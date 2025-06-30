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

#include <algorithm>  // std::random_shuffle
#include <array>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <locale>
#include <memory>
#include <stdexcept>
#include <vector>

namespace ART {

uint8_t flipSign(uint8_t keyByte) {
    // Flip the sign bit, enables signed SSE comparison of unsigned values, used
    // by Node16
    return keyByte ^ 128;
}

void loadKey(uint32_t tid, uint8_t key[]) {
    // Store the key of the tuple into the key vector
    // Implementation is database specific
    reinterpret_cast<uint32_t*>(key)[0] = __builtin_bswap32(tid);
}

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

unsigned min(unsigned a, unsigned b) {
    // Helper function
    return (a < b) ? a : b;
}

int arrToInt(const uint8_t key[4]) {
    return (int32_t(key[0]) << 24) | (int32_t(key[1]) << 16) |
           (int32_t(key[2]) << 8) | (int32_t(key[3]));
}
std::array<uint8_t, 4> intToArr(int value) {
    return {
        static_cast<uint8_t>((value / 16777216) % 256),  // 2^24
        static_cast<uint8_t>((value / 65536) % 256),     // 2^16
        static_cast<uint8_t>((value / 256) % 256),       // 2^8
        static_cast<uint8_t>(value % 256)                // 2^0
    };
}

}  // namespace ART