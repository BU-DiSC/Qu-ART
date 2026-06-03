#pragma once

#include <utility>

#include "../ART.h"
#include "../ArtNode.h"
#include "QuART.h"

namespace ART {

// Eviction policy used by QuART_kfp when all K slots are occupied and a new
// workload (NO_MATCH) arrives.
//
//   FIFO         – always evict the oldest slot (round-robin pointer).
//
//   FREQ_FILTER  – on the FIRST sighting of a new upper-byte prefix, insert
//                  without evicting (cache-pollution guard for outlier keys).
//                  Only on the second consecutive sighting of the same prefix
//                  is a slot actually evicted.  Slots that are never seen
//                  again do not pollute the cache at all.
enum class EvictionPolicy { FIFO, FREQ_FILTER };

// Strategy used by findSlot to classify a key against the K active slots.
//
//   Parallel    – data-parallel branchless FP_INSERT pre-pass: all K
//                 upper-byte equality checks are computed with no inter-check
//                 data dependency, so the CPU issues them together and the
//                 whole classification collapses to a single, well-predicted
//                 branch.  Falls back to the sequential scan for warm-up,
//                 BRIDGE and NO_MATCH.  (default — current behaviour)
//
//   Sequential  – the original early-exit scan: test slots in index order and
//                 return at the first FP_INSERT *or* BRIDGE match.  Simpler,
//                 but every per-slot comparison is a hard-to-predict branch.
//
// Note: the two modes are not perfectly equivalent at the boundary between two
// slots whose upper bytes are adjacent (differ by exactly 1).  Sequential
// returns whatever the lowest-index slot offers (a BRIDGE there can pre-empt an
// exact FP_INSERT in a higher-index slot), whereas Parallel always prefers a
// global FP_INSERT once all K slots are occupied.  Both are valid classifiers;
// they only diverge on this rare adjacent-prefix case.
enum class SearchMode { Sequential, Parallel };

// QuART_kfp<K, Policy, Search>: Maintains up to K independent fast-path slots,
// one per workload.  Uses the stail key-classification scheme (FP_INSERT /
// BRIDGE / NO_MATCH) applied independently against every active slot.
//
// Correctness guarantee: whenever any inner node tracked by any slot is
// expanded (grown to a larger node type), or a prefix mismatch/leaf-expansion
// event touches a tracked node, ALL affected slots are updated immediately via
// the virtual FP hook overrides.  No slot ever goes stale.
//
// Insertion routing:
//   FP_INSERT – key upper-3-bytes match a slot's fp_leaf  → preserve_fp
//   BRIDGE    – key upper-3-bytes adjacent (±1) to a slot  → change_fp, reset
//   NO_MATCH  – no existing slot claims the key            → allocate a slot
//               (eviction policy selected by the Policy template parameter)
//
// The Search template parameter selects how findSlot scans the K slots
// (Parallel branchless vs. Sequential early-exit); see SearchMode above.
template <int K, EvictionPolicy Policy = EvictionPolicy::FIFO,
          SearchMode Search = SearchMode::Parallel>
class QuART_kfp : public QuART {
   public:
    QuART_kfp() : QuART(), num_active(0), next_evict(0), active_slot(-1)
                 , candidate_upper(0), candidate_valid(false)
#ifdef QUART_KFP_STATS
                 , cnt_fp_insert(0), cnt_bridge(0), cnt_no_match(0)
                 , cnt_no_match_untracked(0)
                 , cnt_type4(0), cnt_type16(0), cnt_type48(0), cnt_type256(0)
                 , cnt_fp_not_last_byte(0)
#endif
                 {}

    void insert(uint8_t key[], uintptr_t value) override {
        // Prefetch all cached fp nodes before findSlot so the cache misses are
        // overlapped with the classification work rather than serialised after it.
        for (int i = 0; i < num_active; i++)
            __builtin_prefetch(slots[i].fp, 0 /*read*/, 1 /*L2 locality*/);

        if (root == nullptr) {
            int slot = allocSlot();
            active_slot = slot;
            // Flat fields are null for a fresh slot; hooks see consistent state.
            loadSlot(slot);
            insert_recursive_change_fp(root, &root, key, 0, value,
                                       maxPrefixLength);
            saveToSlot(slot);
            return;
        }

        auto [slotIdx, matchType] = findSlot(key);

        if (__builtin_expect(matchType == MatchType::FP_INSERT, 1)) {
#ifdef QUART_KFP_STATS
            cnt_fp_insert++;
#endif
            active_slot = slotIdx;
            FpSlot& s = slots[slotIdx];

            if (__builtin_expect(s.fp_depth == maxPrefixLength - 2, 1)) {
                // Hot path: direct insert at last-byte level.
                // FpSlot fields are authoritative; every hook override updates
                // slots[slotIdx] via the slot loop, so loadSlot/saveToSlot
                // (which copy to/from the flat fp fields) are not needed here.
                ArtNode* newLeaf = makeLeaf(value);
                // Use cached s.fp_type to avoid dereferencing s.fp on
                // every dispatch (s.fp_type is in the slot struct, L1 hit).
                if (__builtin_expect(s.fp_type == NodeType256, 1)) {
#ifdef QUART_KFP_STATS
                    cnt_type256++;
#endif
                    static_cast<Node256*>(s.fp)->insertNode256(
                        this, s.fp_ref, key[s.fp_depth], newLeaf);
                } else if (s.fp_type == NodeType48) {
#ifdef QUART_KFP_STATS
                    cnt_type48++;
#endif
                    static_cast<Node48*>(s.fp)->insertNode48PreserveFp(
                        this, s.fp_ref, key[s.fp_depth], newLeaf);
                } else if (s.fp_type == NodeType16) {
#ifdef QUART_KFP_STATS
                    cnt_type16++;
#endif
                    static_cast<Node16*>(s.fp)->insertNode16PreserveFp(
                        this, s.fp_ref, key[s.fp_depth], newLeaf);
                } else {
#ifdef QUART_KFP_STATS
                    cnt_type4++;
#endif
                    static_cast<Node4*>(s.fp)->insertNode4PreserveFp(
                        this, s.fp_ref, key[s.fp_depth], newLeaf);
                }
            } else {
#ifdef QUART_KFP_STATS
                cnt_fp_not_last_byte++;
#endif
                loadSlot(slotIdx);
                insert_recursive_preserve_fp(s.fp, s.fp_ref, key, s.fp_depth,
                                             value, maxPrefixLength, s.fp_prev);
                saveToSlot(slotIdx);
            }

        } else if (matchType == MatchType::BRIDGE) {
#ifdef QUART_KFP_STATS
            cnt_bridge++;
#endif
            // Key is adjacent to an existing workload boundary: reset that
            // slot and begin tracking the new page from root.
            active_slot = slotIdx;
            loadSlot(slotIdx);
            insert_recursive_change_fp(root, &root, key, 0, value,
                                       maxPrefixLength);
            saveToSlot(slotIdx);

        } else {  // NO_MATCH – new workload
#ifdef QUART_KFP_STATS
            cnt_no_match++;
#endif
            key_int_t keyUpper = getKeyUpperBytes(key);
            int slot = allocSlotForNoMatch(keyUpper);
            if (slot == -1) {
                // FREQ_FILTER: first sighting — insert without evicting a
                // tracked slot so outlier keys don't pollute the cache.
#ifdef QUART_KFP_STATS
                cnt_no_match_untracked++;
#endif
                fp = nullptr; fp_prev = nullptr; fp_leaf = nullptr;
                fp_depth = 0;  fp_ref  = nullptr;
                active_slot = -1;
                insert_recursive_change_fp(root, &root, key, 0, value,
                                           maxPrefixLength);
            } else {
                active_slot = slot;
                // Null out the flat fields so hooks don't see stale pointers.
                loadSlot(slot);
                insert_recursive_change_fp(root, &root, key, 0, value,
                                           maxPrefixLength);
                saveToSlot(slot);
            }
        }
    }

    int getNumActive() const { return num_active; }
    const FpSlot& getSlot(int i) const { return slots[i]; }
#ifdef QUART_KFP_STATS
    long long getFpInsertCount()          const { return cnt_fp_insert; }
    long long getBridgeCount()            const { return cnt_bridge; }
    long long getNoMatchCount()           const { return cnt_no_match; }
    long long getNoMatchUntrackedCount()  const { return cnt_no_match_untracked; }
    long long getFpType4Count()           const { return cnt_type4; }
    long long getFpType16Count()          const { return cnt_type16; }
    long long getFpType48Count()          const { return cnt_type48; }
    long long getFpType256Count()         const { return cnt_type256; }
    long long getFpNotLastByteCount()     const { return cnt_fp_not_last_byte; }
#endif

   private:
    enum class MatchType { FP_INSERT, BRIDGE, NO_MATCH };

    FpSlot slots[K];
    int num_active;             // number of allocated slots (0..K)
    int next_evict;             // FIFO eviction pointer
    int active_slot;            // which slot is being modified during this insert
    key_int_t candidate_upper;  // FREQ_FILTER: upper bytes of last NO_MATCH key
    bool candidate_valid;       // FREQ_FILTER: whether candidate_upper is set
#ifdef QUART_KFP_STATS
    long long cnt_fp_insert, cnt_bridge, cnt_no_match, cnt_no_match_untracked;
    long long cnt_type4, cnt_type16, cnt_type48, cnt_type256;
    long long cnt_fp_not_last_byte;
#endif

    // ── Slot helpers ─────────────────────────────────────────────────────────

    // Copy a slot's fields into the ART flat fp fields (used before
    // preserve_fp so that the recursive function reads the right values).
    void loadSlot(int i) {
        fp       = slots[i].fp;
        fp_prev  = slots[i].fp_prev;
        fp_leaf  = slots[i].fp_leaf;
        fp_depth = slots[i].fp_depth;
        fp_ref   = slots[i].fp_ref;
    }

    // Flush the ART flat fp fields back into a slot (called after every insert
    // to capture any changes made by the recursive function or hook callbacks).
    void saveToSlot(int i) {
        slots[i].fp       = fp;
        slots[i].fp_prev  = fp_prev;
        slots[i].fp_leaf  = fp_leaf;
        slots[i].fp_depth = fp_depth;
        slots[i].fp_ref   = fp_ref;
        // Only cache type for real inner nodes; fp can briefly be a leaf
        // pointer (after the node==NULL branch in insert_recursive_change_fp)
        // in which case dereferencing ->type would crash.
        slots[i].fp_type  = (fp && !isLeaf(fp)) ? (uint8_t)fp->type : 0;
        // Cache leafUpper for the findSlot fast path.
        // fp_leaf only changes via insert_recursive_change_fp (BRIDGE/NO_MATCH),
        // so this is updated at most a handful of times across the entire run.
        slots[i].cached_upper = fp_leaf ? getLeafUpperBytes(getLeafValue(fp_leaf)) : 0;
    }

    // Return the next available slot index, always evicting (FIFO) if needed.
    // Used for the root==nullptr path where eviction must not be deferred.
    int allocSlot() {
        if (num_active < K) return num_active++;
        int slot = next_evict;
        next_evict = (next_evict + 1) % K;
        return slot;
    }

    // Return the next slot index for a NO_MATCH insert, applying the eviction
    // policy.  Returns -1 when Policy==FREQ_FILTER and this is the first
    // sighting of keyUpper — the caller should insert without tracking.
    int allocSlotForNoMatch(key_int_t keyUpper) {
        if (num_active < K) return num_active++;
        if constexpr (Policy == EvictionPolicy::FREQ_FILTER) {
            if (!candidate_valid || keyUpper != candidate_upper) {
                // First sighting: record as candidate, defer eviction.
                candidate_upper = keyUpper;
                candidate_valid = true;
                return -1;
            }
            // Second consecutive sighting: proceed to evict.
            candidate_valid = false;
        }
        int slot = next_evict;
        next_evict = (next_evict + 1) % K;
        return slot;
    }

    // Classify key against existing slots by comparing all-but-last-byte.
    std::pair<int, MatchType> findSlot(uint8_t key[]) const {
        key_int_t keyUpper = getKeyUpperBytes(key);

        if constexpr (Search == SearchMode::Parallel) {
            // Parallel branchless FP_INSERT classification.
            // All K equality checks are emitted as branchless sete/cmov
            // instructions with no data dependencies between them, so the CPU can
            // issue them in parallel.  The single resulting branch (match != 0) has
            // a very high hit-rate (≈ FP_INSERT%) and is therefore well-predicted,
            // eliminating the branch-misprediction storm from a sequential early-exit
            // loop where each comparison's taken/not-taken result is hard to predict.
            if (__builtin_expect(num_active == K, 1)) {
                unsigned match = 0;
                for (int i = 0; i < K; i++)
                    match |= static_cast<unsigned>(slots[i].cached_upper == keyUpper) << i;
                if (__builtin_expect(match != 0u, 1))
                    return {ctz(match), MatchType::FP_INSERT};
            }
        }

        // Sequential early-exit scan.  In Parallel mode this is the fallback
        // for warm-up, BRIDGE and rare NO_MATCH; in Sequential mode it is the
        // entire classifier.
        for (int i = 0; i < num_active; i++) {
            if (slots[i].fp_leaf == nullptr) continue;
            key_int_t leafUpper = slots[i].cached_upper;
            if (keyUpper == leafUpper)
                return {i, MatchType::FP_INSERT};
            if (((keyUpper + 1) & upperMask) == leafUpper ||
                ((leafUpper + 1) & upperMask) == keyUpper)
                return {i, MatchType::BRIDGE};
        }
        return {-1, MatchType::NO_MATCH};
    }

    // ── FP hook overrides ─────────────────────────────────────────────────────
    //
    // Each override first calls the base-class default (which updates the flat
    // fp/fp_prev/fp_ref fields used by the active slot during traversal), then
    // iterates over ALL K slots and applies the same update to every slot whose
    // fp or fp_prev matches the affected node.  This guarantees no slot ever
    // becomes stale regardless of which workload triggered the structural change.

    // Called when an inner node is replaced by a larger node type.
    void onNodeReplaced(ArtNode* oldNode, ArtNode* newNode,
                        ArtNode** newNodeRef) override {
        // Keep flat fields in sync for the active slot.
        ART::onNodeReplaced(oldNode, newNode, newNodeRef);
        // Update every slot that tracked the replaced node.
        for (int j = 0; j < num_active; j++) {
            if (slots[j].fp == oldNode) {
                slots[j].fp      = newNode;
                slots[j].fp_type = newNode->type;  // keep cache in sync
                slots[j].fp_ref  = newNodeRef;
            } else if (slots[j].fp_prev == oldNode) {
                slots[j].fp_prev = newNode;
                ArtNode** ref = findChildPtr(newNode, slots[j].fp);
                if (ref) slots[j].fp_ref = ref;
            }
        }
    }

    // Called from insertNode4PreserveFpPrefixExpansion when a new parent is
    // spliced above fp and the child cell that holds fp has moved.
    void onFpRefUpdate(ArtNode* targetFp, ArtNode** newRef) override {
        ART::onFpRefUpdate(targetFp, newRef);
        for (int j = 0; j < num_active; j++) {
            if (slots[j].fp == targetFp) slots[j].fp_ref = newRef;
        }
    }

    // Called when the leaf tracked by a slot's fp_leaf is expanded into a
    // Node4.  Adjusts fp_depth using the slot's OWN fp->prefixLength so that
    // every slot that happens to share the same fp_leaf is updated correctly.
    void onLeafExpanded(ArtNode* oldLeaf, Node4* newNode, ArtNode** nodeRef,
                        ArtNode* prevNode) override {
        ART::onLeafExpanded(oldLeaf, newNode, nodeRef, prevNode);
        for (int j = 0; j < num_active; j++) {
            if (slots[j].fp_leaf == oldLeaf) {
                if (!isLeaf(slots[j].fp)) {
                    slots[j].fp_depth += slots[j].fp->prefixLength;
                    slots[j].fp_depth++;
                }
                slots[j].fp      = newNode;
                slots[j].fp_type = newNode->type;  // Node4
                slots[j].fp_ref  = nodeRef;
                slots[j].fp_prev = prevNode;
            }
        }
    }

    // Called when a prefix mismatch splices a new Node4 above fpNode.
    // Updates fp_prev and fp_depth for every slot that was tracking fpNode.
    void onPrefixMismatch(ArtNode* fpNode, Node4* newParent,
                          unsigned mismatchPrefixLen) override {
        ART::onPrefixMismatch(fpNode, newParent, mismatchPrefixLen);
        for (int j = 0; j < num_active; j++) {
            if (slots[j].fp == fpNode) {
                slots[j].fp_prev  = newParent;
                slots[j].fp_depth += mismatchPrefixLen + 1;
            }
        }
    }

    // Called after a sorted insertion into `node` that used memmove to shift
    // existing children.  For every slot where fp_prev == node, re-derive
    // fp_ref by scanning the node for the slot's fp.
    void onParentShifted(ArtNode* node) override {
        ART::onParentShifted(node);
        for (int j = 0; j < num_active; j++) {
            if (slots[j].fp_prev == node) {
                ArtNode** ref = findChildPtr(node, slots[j].fp);
                if (ref) slots[j].fp_ref = ref;
            }
        }
    }
};

}  // namespace ART
