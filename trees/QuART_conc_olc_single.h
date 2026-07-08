#pragma once

#include <emmintrin.h>  // _mm_pause

#include <array>
#include <atomic>
#include <cstdint>
#include <utility>
#include <vector>

#include "../ART.h"
#include "../ArtNode.h"
#include "QuART.h"
#include "olc_common.h"

namespace ART {

// ─────────────────────────────────────────────────────────────────────────────
// Single-STREAM OLC variants — the two ends of a controlled A/B comparison.
//
// Both are concurrent ARTs that use Optimistic Lock Coupling (OLC) on the
// structural descent (lock-free versioned reads; exclusive lock only on the
// mutated node + its parent) — identical machinery to QuART_conc_olc.  They
// differ in ONE thing, the presence of a fast path:
//
//   • QuART_olc_single_art   : plain OLC ART.  NO fast path at all — every insert
//                            is a full lock-coupled descent from the root.  This
//                            is the canonical ARTOLC baseline.
//
//   • QuART_olc_single_stail : OLC ART + ONE globally SHARED fast-path tail (the
//                            "stail" = single tail).  All threads read/append to
//                            a single shared fp guarded by its own version word
//                            (fpVer_): a near-sorted append snapshots the shared
//                            fp lock-free, exclusive-locks only the tail node,
//                            re-validates the fp version, and appends.  The slow
//                            path publishes the new tail under fpVer_.  This is
//                            the concurrent, OLC-read realization of the old
//                            single-shared-fp idea (cf. the deleted conc_stail,
//                            which used a seqlock + a serializing writer lock).
//
// The comparison isolates the value of a SINGLE shared fast path under OLC
// concurrency for ONE stream inserted by many threads.  (For MANY streams, the
// per-thread-tail QuART_conc_olc is the right tool; a single shared tail cannot
// follow more than one stream's tail at a time.)
//
// Same memory-safety / ThreadSanitizer story as QuART_conc_olc: reclamation is
// deferred (NodeTypeObsolete + retire lists) so optimistic reads never touch
// freed memory; OLC's lock-free reads race with locked writers by construction
// (benign, version-validated) so this is ASan-clean + verify-every-key-clean but
// NOT TSan-clean.  Intended use is parallel-insert → join() → verify; concurrent
// lookups are not supported.
// ─────────────────────────────────────────────────────────────────────────────

// ── Shared base: all OLC machinery + a fp-agnostic lock-coupled insert ────────
// descendAndInsert reports the terminal inner node it built/appended into via
// out-params, so a subclass can (stail) publish it as the shared tail or
// (art) ignore it.
class QuART_olc_single_base : public QuART {
   public:
    // stripes_ (NSTRIPES × 64B ≈ 8 MB) is heap-allocated via std::vector so the
    // tree object stays small enough to live on the test's stack.
    QuART_olc_single_base() : QuART(), stripes_(NSTRIPES) {}

    ~QuART_olc_single_base() {
        for (auto& sh : retire_)
            for (auto& pr : sh.v) freeByType(pr.first, pr.second);
    }

   protected:
    static constexpr unsigned NSTRIPES = 1u << 17;  // 131072 version words
    static constexpr unsigned NRETIRE = 512;        // retire-list shards

    struct alignas(64) RetireShard {
        OlcLock lk;  // reuse a version word purely as a mutex for the shard
        std::vector<std::pair<ArtNode*, int8_t>> v;
    };

    std::vector<OlcLock> stripes_;  // per-node version words (striped)
    OlcLock rootVer_;               // guards the &root slot
    std::array<RetireShard, NRETIRE> retire_;

    // Map a node's address to its version word (stable for the run — deferred
    // reclamation).  Distinct live nodes may share a word (conservative: an
    // occasional spurious restart, never unsafe).
    OlcLock& stripeVer(const ArtNode* n) {
        uintptr_t p = reinterpret_cast<uintptr_t>(n) >> 4;  // nodes are ≥16B
        p *= 0x9E3779B97F4A7C15ull;
        return stripes_[(p >> 29) & (NSTRIPES - 1)];
    }

    void reclaimNode(ArtNode* n) override {
        int8_t t = n->type;
        n->type = NodeTypeObsolete;
        uintptr_t p = reinterpret_cast<uintptr_t>(n) >> 4;
        p *= 0x9E3779B97F4A7C15ull;
        RetireShard& sh = retire_[(p >> 17) & (NRETIRE - 1)];
        uint64_t lv = olcWriteLock(sh.lk.w);
        sh.v.emplace_back(n, t);
        olcWriteUnlock(sh.lk.w, lv);
    }

    static void freeByType(ArtNode* n, int8_t t) {
        switch (t) {
            case NodeType4:
                delete static_cast<Node4*>(n);
                break;
            case NodeType16:
                delete static_cast<Node16*>(n);
                break;
            case NodeType48:
                delete static_cast<Node48*>(n);
                break;
            case NodeType256:
                delete static_cast<Node256*>(n);
                break;
            default:
                delete n;
                break;
        }
    }

    static bool wouldGrow(ArtNode* n) {
        switch (n->type) {
            case NodeType4:
                return n->count >= 4;
            case NodeType16:
                return n->count >= 16;
            case NodeType48:
                return n->count >= 48;
            default:  // Node256 never grows
                return false;
        }
    }

    void insertChild(ArtNode* node, ArtNode** nodeRef, uint8_t b,
                     ArtNode* leaf) {
        switch (node->type) {
            case NodeType4:
                static_cast<Node4*>(node)->insertNode4(this, nodeRef, b, leaf);
                break;
            case NodeType16:
                static_cast<Node16*>(node)->insertNode16(this, nodeRef, b,
                                                         leaf);
                break;
            case NodeType48:
                static_cast<Node48*>(node)->insertNode48(this, nodeRef, b,
                                                         leaf);
                break;
            case NodeType256:
                static_cast<Node256*>(node)->insertNode256(this, nodeRef, b,
                                                           leaf);
                break;
        }
    }

    bool lockTwo(OlcLock* parentLk, uint64_t pv, OlcLock* nodeLk, uint64_t v) {
        if (parentLk == nodeLk) {
            if (pv != v) return false;
            return olcTryUpgrade(nodeLk->w, v);
        }
        if (!olcTryUpgrade(parentLk->w, pv)) return false;
        if (!olcTryUpgrade(nodeLk->w, v)) {
            olcWriteUnlock(parentLk->w, pv);
            return false;
        }
        return true;
    }
    void unlockTwo(OlcLock* parentLk, uint64_t pv, OlcLock* nodeLk,
                   uint64_t v) {
        if (parentLk == nodeLk) {
            olcWriteUnlock(nodeLk->w, v);
            return;
        }
        olcWriteUnlock(nodeLk->w, v);
        olcWriteUnlock(parentLk->w, pv);
    }

    // One optimistic lock-coupled descent+mutation.  Returns true on success,
    // false on a version conflict (caller restarts from the root).  Reports the
    // terminal inner node it built/appended into: on success, if outHasTail is
    // set, (outFp, outRef, outDepth) is a Node* that branches on key[outDepth]
    // and now holds the new leaf (usable as a fast-path tail); outHasTail is
    // false only for the first-key (root becomes a bare leaf) case.
    bool descendAndInsert(uint8_t key[], uintptr_t value, ArtNode*& outFp,
                          ArtNode**& outRef, size_t& outDepth,
                          bool& outHasTail) {
        outHasTail = false;
        auto setTail = [&](ArtNode* fp, ArtNode** ref, unsigned d) {
            outFp = fp;
            outRef = ref;
            outDepth = d;
            outHasTail = true;
        };

        OlcLock* parentLk = &rootVer_;  // guards *nodeRef (== &root at the top)
        uint64_t pv = olcReadLock(parentLk->w);
        ArtNode** nodeRef = &root;
        ArtNode* node = root;
        unsigned depth = 0;

        for (;;) {
            if (node == nullptr) {  // empty slot: plant a leaf (slot guarded)
                if (!olcTryUpgrade(parentLk->w, pv)) return false;
                *nodeRef = makeLeaf(value);
                olcWriteUnlock(parentLk->w, pv);
                return true;  // no inner tail (first key)
            }

            if (isLeaf(node)) {
                // Root (or any slot) holds a bare leaf: expand it into a Node4.
                if (!olcTryUpgrade(parentLk->w, pv)) return false;
                uint8_t existingKey[maxPrefixLength];
                loadKey(getLeafValue(node), existingKey);
                unsigned npl = 0;
                while (existingKey[depth + npl] == key[depth + npl]) npl++;
                Node4* nn = new Node4();
                nn->prefixLength = npl;
                memcpy(nn->prefix, key + depth, min(npl, maxPrefixLength));
                *nodeRef = nn;
                nn->insertNode4(this, nodeRef, existingKey[depth + npl], node);
                nn->insertNode4(this, nodeRef, key[depth + npl],
                                makeLeaf(value));
                setTail(nn, nodeRef, depth + npl);
                olcWriteUnlock(parentLk->w, pv);
                return true;
            }

            OlcLock* nodeLk = &stripeVer(node);
            uint64_t v = olcReadLock(nodeLk->w);
            if (!olcCheck(parentLk->w, pv)) return false;

            if (node->prefixLength) {
                unsigned mm = prefixMismatch(node, key, depth, maxPrefixLength);
                if (!olcCheck(nodeLk->w, v)) return false;
                if (mm != node->prefixLength) {
                    // ── Prefix split: rewrites *nodeRef (parent) AND node ──
                    if (!lockTwo(parentLk, pv, nodeLk, v)) return false;
                    Node4* nn = new Node4();
                    *nodeRef = nn;
                    nn->prefixLength = mm;
                    memcpy(nn->prefix, node->prefix, min(mm, maxPrefixLength));
                    if (node->prefixLength < maxPrefixLength) {
                        nn->insertNode4(this, nodeRef, node->prefix[mm], node);
                        node->prefixLength -= (mm + 1);
                        memmove(node->prefix, node->prefix + mm + 1,
                                min(node->prefixLength, maxPrefixLength));
                    } else {
                        node->prefixLength -= (mm + 1);
                        uint8_t minKey[maxPrefixLength];
                        loadKey(getLeafValue(minimum(node)), minKey);
                        nn->insertNode4(this, nodeRef, minKey[depth + mm],
                                        node);
                        memmove(node->prefix, minKey + depth + mm + 1,
                                min(node->prefixLength, maxPrefixLength));
                    }
                    nn->insertNode4(this, nodeRef, key[depth + mm],
                                    makeLeaf(value));
                    setTail(nn, nodeRef, depth + mm);
                    unlockTwo(parentLk, pv, nodeLk, v);
                    return true;
                }
                depth += node->prefixLength;
            }

            ArtNode** childRef = findChild(node, key[depth]);
            ArtNode* child = *childRef;
            if (!olcCheck(nodeLk->w, v)) return false;

            if (child == nullptr) {
                // ── Add child to `node` (may grow ⇒ rewrites *nodeRef) ──
                if (wouldGrow(node)) {
                    if (!lockTwo(parentLk, pv, nodeLk, v)) return false;
                    insertChild(node, nodeRef, key[depth], makeLeaf(value));
                    setTail(*nodeRef, nodeRef, depth);  // grown node = *nodeRef
                    unlockTwo(parentLk, pv, nodeLk, v);
                } else {
                    if (!olcTryUpgrade(nodeLk->w, v)) return false;
                    insertChild(node, nodeRef, key[depth], makeLeaf(value));
                    setTail(node, nodeRef, depth);
                    olcWriteUnlock(nodeLk->w, v);
                }
                return true;
            }

            if (isLeaf(child)) {
                // ── Expand leaf → Node4 (rewrites *childRef, inside `node`) ──
                if (!olcTryUpgrade(nodeLk->w, v)) return false;
                uint8_t existingKey[maxPrefixLength];
                loadKey(getLeafValue(child), existingKey);
                unsigned d2 = depth + 1, npl = 0;
                while (existingKey[d2 + npl] == key[d2 + npl]) npl++;
                Node4* nn = new Node4();
                nn->prefixLength = npl;
                memcpy(nn->prefix, key + d2, min(npl, maxPrefixLength));
                *childRef = nn;
                nn->insertNode4(this, childRef, existingKey[d2 + npl], child);
                nn->insertNode4(this, childRef, key[d2 + npl], makeLeaf(value));
                setTail(nn, childRef, d2 + npl);
                olcWriteUnlock(nodeLk->w, v);
                return true;
            }

            // ── Descend: validate & release parent, hand off to `node` ──
            if (!olcCheck(parentLk->w, pv)) return false;
            parentLk = nodeLk;
            pv = v;
            nodeRef = childRef;
            node = child;
            depth += 1;
        }
    }

    void slowInsert(uint8_t key[], uintptr_t value, ArtNode*& outFp,
                    ArtNode**& outRef, size_t& outDepth, bool& outHasTail) {
        while (!descendAndInsert(key, value, outFp, outRef, outDepth,
                                 outHasTail)) {
            for (int b = 0; b < 24; b++) _mm_pause();  // backoff, then restart
        }
    }
};

// ── Plain OLC ART: no fast path ───────────────────────────────────────────────
class QuART_olc_single_art : public QuART_olc_single_base {
   public:
    explicit QuART_olc_single_art(int /*numThreads*/) : QuART_olc_single_base() {}

    void insert(uint8_t key[], uintptr_t value) override {
        insert(0, key, value);
    }

    void insert(int /*tid*/, uint8_t key[], uintptr_t value) {
        ArtNode* f;
        ArtNode** r;
        size_t d;
        bool h;
        slowInsert(key, value, f, r, d, h);
    }
};

// ── OLC ART + one globally shared fast-path tail ──────────────────────────────
class QuART_olc_single_stail : public QuART_olc_single_base {
   public:
    explicit QuART_olc_single_stail(int /*numThreads*/)
        : QuART_olc_single_base() {}

    void insert(uint8_t key[], uintptr_t value) override {
        insert(0, key, value);
    }

    void insert(int /*tid*/, uint8_t key[], uintptr_t value) {
        const key_int_t keyUpper = getKeyUpperBytes(key);

        // ── Fast path: append into the single shared tail (near-sorted) ──
        // Snapshot the shared fp lock-free; if it is the terminal tail for
        // this key's prefix, exclusive-lock ONLY that tail node, re-validate
        // the fp version (it must not have advanced), and append one leaf.
        uint64_t fv = olcReadLock(fpVer_.w);
        ArtNode* fp = sh_fp;
        ArtNode** fp_ref = sh_fp_ref;
        size_t fp_depth = sh_fp_depth;
        key_int_t cu = sh_cached_upper;
        bool init = sh_initialized;
        // Snapshot consistent ⇒ fp is a real (possibly-obsolete) node pointer
        // that deferred reclamation keeps safe to dereference.
        if (olcCheck(fpVer_.w, fv) && init && fp && keyUpper == cu &&
            fp_depth == (size_t)maxPrefixLength - 2) {
            std::atomic<uint64_t>& w = stripeVer(fp).w;
            uint64_t lv = olcWriteLock(w);
            if (olcCheck(fpVer_.w, fv) && fp->type != NodeTypeObsolete) {
                const uint8_t b = key[fp_depth];
                ArtNode* leaf = makeLeaf(value);
                switch (fp->type) {
                    case NodeType256:
                        static_cast<Node256*>(fp)->insertNode256(this, fp_ref, b,
                                                                 leaf);
                        olcWriteUnlock(w, lv);
                        return;
                    case NodeType4:
                        if (fp->count < 4) {
                            static_cast<Node4*>(fp)->insertNode4(this, fp_ref, b,
                                                                 leaf);
                            olcWriteUnlock(w, lv);
                            return;
                        }
                        break;
                    case NodeType16:
                        if (fp->count < 16) {
                            static_cast<Node16*>(fp)->insertNode16(this, fp_ref,
                                                                   b, leaf);
                            olcWriteUnlock(w, lv);
                            return;
                        }
                        break;
                    case NodeType48:
                        if (fp->count < 48) {
                            static_cast<Node48*>(fp)->insertNode48(this, fp_ref,
                                                                   b, leaf);
                            olcWriteUnlock(w, lv);
                            return;
                        }
                        break;
                }
                // would-grow leaf we allocated is unused; harmless (leaked to
                // the intentional live-tree leak).  Fall through to slow path.
            }
            olcWriteUnlock(w, lv);  // obsolete / advanced / would-grow ⇒ slow
        }

        // ── Slow path: OLC descent, then publish the new shared tail ──
        ArtNode* f;
        ArtNode** r;
        size_t d;
        bool h;
        slowInsert(key, value, f, r, d, h);

        uint64_t pv = olcWriteLock(fpVer_.w);
        if (h) {
            sh_fp = f;
            sh_fp_ref = r;
            sh_fp_depth = d;
            sh_cached_upper = keyUpper;
            sh_initialized = true;
        } else {
            sh_fp = nullptr;
            sh_initialized = false;
        }
        olcWriteUnlock(fpVer_.w, pv);
    }

   private:
    OlcLock fpVer_;  // guards the shared fp snapshot below
    ArtNode* sh_fp = nullptr;
    ArtNode** sh_fp_ref = nullptr;
    size_t sh_fp_depth = 0;
    key_int_t sh_cached_upper = 0;
    bool sh_initialized = false;
};

}  // namespace ART
