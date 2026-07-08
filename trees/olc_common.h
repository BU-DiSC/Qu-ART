#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// olc_common.h — Optimistic Lock Coupling (OLC) version-word primitives, shared
// by the OLC-family concurrent QuART variants.
//
// A 64-bit word encodes  [ version : 63 bits ][ write-locked : 1 bit (LSB) ].
// The protocol is exactly that of Leis et al., "The ART of Practical
// Synchronization" (DaMoN 2016) — the same authors as ART itself:
//   readLock(w)       spin while LSB set, then return the even snapshot v.
//   check(w, v)       (load == v): false ⇒ node changed ⇒ RESTART.
//   tryUpgrade(w, v)  CAS v -> v|1: take the exclusive lock (fails if the
//                     version moved, i.e. a concurrent writer touched it).
//   writeUnlock(w, v) store v+2: clears the lock, bumps the version so any
//                     optimistic reader that saw v now restarts.
//   writeLock(w)      readLock then tryUpgrade in a loop.
//
// Words are cache-line padded (OlcLock) so hot version lines (the root, upper
// levels) don't false-share with an unrelated stripe.  ArtNode is left
// byte-for-byte untouched — version words live in an external striped table.
// ─────────────────────────────────────────────────────────────────────────────

#include <emmintrin.h>  // _mm_pause

#include <atomic>
#include <cstdint>

namespace ART {

struct alignas(64) OlcLock {
    std::atomic<uint64_t> w{0};
    char pad[64 - sizeof(std::atomic<uint64_t>)];
};

static inline uint64_t olcReadLock(std::atomic<uint64_t>& w) {
    for (;;) {
        uint64_t v = w.load(std::memory_order_acquire);
        if (!(v & 1)) return v;  // even ⇒ unlocked
        _mm_pause();
    }
}
static inline bool olcCheck(std::atomic<uint64_t>& w, uint64_t v) {
    return w.load(std::memory_order_acquire) == v;
}
static inline bool olcTryUpgrade(std::atomic<uint64_t>& w, uint64_t v) {
    return w.compare_exchange_strong(v, v | 1, std::memory_order_acquire,
                                     std::memory_order_relaxed);
}
static inline void olcWriteUnlock(std::atomic<uint64_t>& w, uint64_t v) {
    w.store(v + 2, std::memory_order_release);  // clear lock, bump version
}
static inline uint64_t olcWriteLock(std::atomic<uint64_t>& w) {
    for (;;) {
        uint64_t v = olcReadLock(w);
        if (olcTryUpgrade(w, v)) return v;
    }
}

}  // namespace ART
