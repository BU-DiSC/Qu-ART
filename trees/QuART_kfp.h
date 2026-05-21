#pragma once

#include <utility>

#include "../ART.h"
#include "../ArtNode.h"
#include "QuART.h"

namespace ART {

// QuART_kfp<K>: Maintains up to K independent fast-path slots, one per
// workload.  Uses the stail key-classification scheme (FP_INSERT / BRIDGE /
// NO_MATCH) applied independently against every active slot.
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
//               (FIFO eviction when all K are occupied)
template <int K>
class QuART_kfp : public QuART {
   public:
    QuART_kfp() : QuART(), num_active(0), next_evict(0), active_slot(-1),
                   cnt_fp_insert(0), cnt_bridge(0), cnt_no_match(0) {}

    void insert(uint8_t key[], uintptr_t value) override {
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

        if (matchType == MatchType::FP_INSERT) {
            cnt_fp_insert++;
            active_slot = slotIdx;
            FpSlot& s = slots[slotIdx];

            if (s.fp_depth == maxPrefixLength - 2) {
                // Hot path: direct insert at last-byte level.
                // FpSlot fields are authoritative; every hook override updates
                // slots[slotIdx] via the slot loop, so loadSlot/saveToSlot
                // (which copy to/from the flat fp fields) are not needed here.
                ArtNode* newLeaf = makeLeaf(value);
                switch (s.fp->type) {
                    case NodeType4:
                        static_cast<Node4*>(s.fp)->insertNode4PreserveFp(
                            this, s.fp_ref, key[s.fp_depth], newLeaf);
                        break;
                    case NodeType16:
                        static_cast<Node16*>(s.fp)->insertNode16PreserveFp(
                            this, s.fp_ref, key[s.fp_depth], newLeaf);
                        break;
                    case NodeType48:
                        static_cast<Node48*>(s.fp)->insertNode48PreserveFp(
                            this, s.fp_ref, key[s.fp_depth], newLeaf);
                        break;
                    case NodeType256:
                        static_cast<Node256*>(s.fp)->insertNode256(
                            this, s.fp_ref, key[s.fp_depth], newLeaf);
                        break;
                }
            } else {
                loadSlot(slotIdx);
                insert_recursive_preserve_fp(s.fp, s.fp_ref, key, s.fp_depth,
                                             value, maxPrefixLength, s.fp_prev);
                saveToSlot(slotIdx);
            }

        } else if (matchType == MatchType::BRIDGE) {
            cnt_bridge++;
            // Key is adjacent to an existing workload boundary: reset that
            // slot and begin tracking the new page from root.
            active_slot = slotIdx;
            loadSlot(slotIdx);
            insert_recursive_change_fp(root, &root, key, 0, value,
                                       maxPrefixLength);
            saveToSlot(slotIdx);

        } else {  // NO_MATCH – new workload
            cnt_no_match++;
            int slot = allocSlot();
            active_slot = slot;
            // Null out the flat fields so hooks don't see stale pointers.
            loadSlot(slot);
            insert_recursive_change_fp(root, &root, key, 0, value,
                                       maxPrefixLength);
            saveToSlot(slot);
        }
    }

    int getNumActive() const { return num_active; }
    const FpSlot& getSlot(int i) const { return slots[i]; }
    long long getFpInsertCount()  const { return cnt_fp_insert; }
    long long getBridgeCount()    const { return cnt_bridge; }
    long long getNoMatchCount()   const { return cnt_no_match; }

   private:
    enum class MatchType { FP_INSERT, BRIDGE, NO_MATCH };

    FpSlot slots[K];
    int num_active;   // number of allocated slots (0..K)
    int next_evict;   // FIFO eviction pointer
    int active_slot;  // which slot is being modified during this insert
    long long cnt_fp_insert, cnt_bridge, cnt_no_match;

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
    }

    // Return the next available slot index, evicting (FIFO) if all K are full.
    int allocSlot() {
        if (num_active < K) return num_active++;
        int slot = next_evict;
        next_evict = (next_evict + 1) % K;
        return slot;
    }

    // Classify key against existing slots by comparing upper-3-bytes.
    std::pair<int, MatchType> findSlot(uint8_t key[]) const {
        uint32_t keyUpper =
            ((uint32_t)key[0] << 16) | ((uint32_t)key[1] << 8) | key[2];

        for (int i = 0; i < num_active; i++) {
            if (slots[i].fp_leaf == nullptr) continue;
            uint32_t leafUpper =
                (uint32_t)(getLeafValue(slots[i].fp_leaf) >> 8) & 0xFFFFFF;

            if (keyUpper == leafUpper)
                return {i, MatchType::FP_INSERT};

            if (((keyUpper + 1) & 0xFFFFFF) == leafUpper ||
                ((leafUpper + 1) & 0xFFFFFF) == keyUpper)
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
                slots[j].fp     = newNode;
                slots[j].fp_ref = newNodeRef;
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
