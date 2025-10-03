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

// Throws a RestartException to abort the current operation and force a retry
[[noreturn]] inline void restart() { throw RestartException{}; }

// CPU pause instruction to reduce power consumption and improve performance in spin loops
inline void pause() {
    _mm_pause();
}

// Waits until the node is unlocked and returns the version when it becomes available
// Spins while the lock bit (bit 1) is set, yielding CPU cycles with pause()
inline uint64_t awaitNodeUnlocked(ArtNode* node) {
    uint64_t version = node->version.load();
    while ((version & 2) == 2) {   // spin while locked
        pause();
        version = node->version.load();
    }
    return version;
}

// Sets the lock bit (bit 1) in a version number by adding 2
// Used to create a locked version for compare-and-swap operations
inline uint64_t setLockedBit(uint64_t version) {
    return version + 2;
}

// Checks if a node version indicates the node is obsolete (bit 0 is set)
// Obsolete nodes should not be used and operations should restart
inline bool isObsolete(uint64_t version) {
    return (version & 1) == 1;
}

// Releases a write lock by incrementing version by 2
// This clears the lock bit (bit 1) and increments the version counter
inline void writeUnlock(ArtNode* node) {
    // reset locked bit and overflow into version
    node->version.fetch_add(2);
}

// Releases a write lock and marks the node as obsolete by incrementing version by 3
// This sets obsolete bit (bit 0), clears lock bit (bit 1), and increments version
inline void writeUnlockObsolete(ArtNode* node) {
    // set obsolete, reset locked, overflow into version
    node->version.fetch_add(3);
}

// Acquires a read lock by waiting for the node to be unlocked and checking if it's obsolete
// Returns the version number to be used for validation later
inline uint64_t readLockOrRestart(ArtNode* node) {
    uint64_t version = awaitNodeUnlocked(node);
    if (isObsolete(version)) {
        restart();
    }
    return version;
}

// Validates that the node version hasn't changed since the read lock was acquired
// If version changed, another thread modified the node, so restart the operation
inline void readUnlockOrRestart(ArtNode* node, uint64_t version) {
    if (version != node->version.load()) {
        restart();
    }
}

// Same as readUnlockOrRestart but releases a held write lock before restarting
// Used when holding a lock on another node that needs to be released on restart
inline void readUnlockOrRestart(ArtNode* node, uint64_t version, ArtNode* lockedNode) {
    if (version != node->version.load()) {
        writeUnlock(lockedNode);
        restart();
    }
}

// Alias for readUnlockOrRestart - validates version hasn't changed
// Provides semantic clarity when used for general version checking
inline void checkOrRestart(ArtNode* node, uint64_t version) {
    readUnlockOrRestart(node, version);
}

// Atomically upgrades a read lock to a write lock using compare-and-swap
// If another thread changed the version, the CAS fails and operation restarts
inline void upgradeToWriteLockOrRestart(ArtNode* node, uint64_t version) {
    if (!node->version.compare_exchange_strong(version, setLockedBit(version))) {
        restart();
    }
}

// Same as upgradeToWriteLockOrRestart but releases a held lock before restarting
// Used when holding a write lock on another node during the upgrade attempt
inline void upgradeToWriteLockOrRestart(ArtNode* node, uint64_t version, ArtNode* lockedNode) {
    if (!node->version.compare_exchange_strong(version, setLockedBit(version))) {
        writeUnlock(lockedNode);
        restart();
    }
}

// Convenience function that acquires a read lock and immediately upgrades to write lock
// Combines readLockOrRestart() and upgradeToWriteLockOrRestart() in one call
inline void writeLockOrRestart(ArtNode* node) {
    // Single-attempt upgrade; CAS failure triggers restart() inside upgrade
    uint64_t version = readLockOrRestart(node);
    upgradeToWriteLockOrRestart(node, version);
}

} // namespace ART