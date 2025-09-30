#pragma once

#include <assert.h>
#include <emmintrin.h>  // x86 SSE intrinsics
#include <immintrin.h>  // AVX512
#include <stdint.h>     // integer types
#include <stdio.h>
#include <stdlib.h>     // malloc, free
#include <string.h>     // memset, memcpy
#include <sys/time.h>   // gettime

#include <algorithm>    // std::random_shuffle
#include <cassert>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <locale>
#include <memory>
#include <stdexcept>
#include <array>

#include "Helper.h"     // Helper functions
#include "ArtNode.h"    // ArtNode* definitions

namespace ART {

// Signal used to restart an OLC attempt
struct RestartException {};
[[noreturn]] inline void restart() { throw RestartException{}; }

// CPU pause for spin loops
inline void pause() {
    _mm_pause();
}

inline uint64_t awaitNodeUnlocked(ArtNode* node) {
    uint64_t version = node->version.load();
    while ((version & 2) == 2) {   // spin while locked
        pause();
        version = node->version.load();
    }
    return version;
}

inline uint64_t setLockedBit(uint64_t version) {
    return version + 2;
}

inline bool isObsolete(uint64_t version) {
    return (version & 1) == 1;
}

// Write unlock helpers before users
inline void writeUnlock(ArtNode* node) {
    // reset locked bit and overflow into version
    node->version.fetch_add(2);
}

inline void writeUnlockObsolete(ArtNode* node) {
    // set obsolete, reset locked, overflow into version
    node->version.fetch_add(3);
}

// Read-side helpers
inline uint64_t readLockOrRestart(ArtNode* node) {
    uint64_t version = awaitNodeUnlocked(node);
    if (isObsolete(version)) {
        restart();
    }
    return version;
}

inline void readUnlockOrRestart(ArtNode* node, uint64_t version) {
    if (version != node->version.load()) {
        restart();
    }
}

inline void readUnlockOrRestart(ArtNode* node, uint64_t version, ArtNode* lockedNode) {
    if (version != node->version.load()) {
        writeUnlock(lockedNode);
        restart();
    }
}

inline void checkOrRestart(ArtNode* node, uint64_t version) {
    readUnlockOrRestart(node, version);
}

// Write upgrade/lock
inline void upgradeToWriteLockOrRestart(ArtNode* node, uint64_t version) {
    if (!node->version.compare_exchange_strong(version, setLockedBit(version))) {
        restart();
    }
}

inline void upgradeToWriteLockOrRestart(ArtNode* node, uint64_t version, ArtNode* lockedNode) {
    if (!node->version.compare_exchange_strong(version, setLockedBit(version))) {
        writeUnlock(lockedNode);
        restart();
    }
}

inline void writeLockOrRestart(ArtNode* node) {
    // Single-attempt upgrade; CAS failure triggers restart() inside upgrade
    uint64_t version = readLockOrRestart(node);
    upgradeToWriteLockOrRestart(node, version);
}

} // namespace ART