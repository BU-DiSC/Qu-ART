#pragma once

#include "../ArtNode.h"
#include <atomic>
#include <stdint.h>
#include <exception>

#ifdef _MSC_VER
#include <intrin.h>
#define PAUSE() _mm_pause()
#else
#include <emmintrin.h>
#define PAUSE() _mm_pause()
#endif

namespace ART {

class RestartException : public std::exception {
public:
    const char* what() const noexcept override {
        return "Operation needs to restart";
    }
};

// Version control function declarations - matching your implementation
uint64_t readLockOrRestart(ArtNode* node);
void checkOrRestart(ArtNode* node, uint64_t version);
void readUnlockOrRestart(ArtNode* node, uint64_t version);
void readUnlockOrRestart(ArtNode* node, uint64_t version, ArtNode* lockedNode);
void upgradeToWriteLockOrRestart(ArtNode* node, uint64_t version);
void upgradeToWriteLockOrRestart(ArtNode* node, uint64_t version, ArtNode* lockedNode);
void writeLockOrRestart(ArtNode* node);
void writeUnlock(ArtNode* node);
void writeUnlockObsolete(ArtNode* node);

// Helper functions
uint64_t awaitNodeUnlocked(ArtNode* node);
uint64_t setLockedBit(uint64_t version);
bool isObsolete(uint64_t version);

} // namespace ART