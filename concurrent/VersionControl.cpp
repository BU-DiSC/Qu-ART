/*
 * Implementation file for concurrency version control functions. Taken directly from 
 * "The ART of Practical Synchronization" paper. 
 */

#include "VersionControl.h"

namespace ART {

uint64_t readLockOrRestart(ArtNode* node) {
    uint64_t version = awaitNodeUnlocked(node);
    if (isObsolete(version)) {
        throw RestartException();
    }
    return version;
}

void checkOrRestart(ArtNode* node, uint64_t version) {
    readUnlockOrRestart(node, version);
}

void readUnlockOrRestart(ArtNode* node, uint64_t version) {
    if (version != node->version.load()) {
        throw RestartException();
    }
}

void readUnlockOrRestart(ArtNode* node, uint64_t version, ArtNode* lockedNode) {
    if (version != node->version.load()) {
        writeUnlock(lockedNode);
        throw RestartException();
    }
}

void upgradeToWriteLockOrRestart(ArtNode* node, uint64_t version) {
    uint64_t expected = version;
    if (!node->version.compare_exchange_strong(expected, setLockedBit(version))) {
        throw RestartException();
    }
}

void upgradeToWriteLockOrRestart(ArtNode* node, uint64_t version, ArtNode* lockedNode) {
    uint64_t expected = version;
    if (!node->version.compare_exchange_strong(expected, setLockedBit(version))) {
        if (lockedNode) { writeUnlock(lockedNode); }
        throw RestartException();
    }
}

void writeLockOrRestart(ArtNode* node) {
    uint64_t version;
    do {
        version = readLockOrRestart(node);
        try {
            upgradeToWriteLockOrRestart(node, version);
            break; // Successfully acquired write lock
        } catch (const RestartException&) {
            // Continue loop to retry
        }
    } while (true);
}

void writeUnlock(ArtNode* node) {
    // reset locked bit and overflow into version
    node->version.fetch_add(2);
}

void writeUnlockObsolete(ArtNode* node) {
    // set obsolete, reset locked, overflow into version
    node->version.fetch_add(3);
}

uint64_t awaitNodeUnlocked(ArtNode* node) {
    uint64_t version = node->version.load();
    while ((version & 2) == 2) { // spinlock while locked
        PAUSE();
        version = node->version.load();
    }
    return version;
}

uint64_t setLockedBit(uint64_t version) {
    return version + 2;
}

bool isObsolete(uint64_t version) {
    return (version & 1) == 1;
}

} // namespace ART