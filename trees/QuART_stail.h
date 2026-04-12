#pragma once

#include "../ART.h"
#include "../ArtNode.h"
#include "QuART.h"

enum class KeyType { FP_INSERT, BRIDGE, OTHER };

namespace ART {

class QuART_stail : public QuART {
   private:
    static constexpr int RESET_COUNTER_INIT = 16;
    int reset_counter;

   public:
    QuART_stail() : QuART(), reset_counter(RESET_COUNTER_INIT) {}

    void insert(uint8_t key[], uintptr_t value) {
        /* stail insert */

        ArtNode* root = this->root;
        
        // If the root is null, insert from scratch and change fp
        if (root == nullptr) {
            insert_recursive_change_fp(
                this->root, &this->root, key, 0, value, maxPrefixLength);
            return;
        }

        KeyType type = getKeyType(key);

        if (type == KeyType::FP_INSERT) {
            if (this->reset_counter != RESET_COUNTER_INIT)
                this->reset_counter = RESET_COUNTER_INIT;
            if (this->fp_depth == maxPrefixLength - 1) {
                // Insert leaf into fp
                ArtNode* newNode = makeLeaf(value);
                switch (this->fp->type) {
                    case NodeType4:
                        static_cast<Node4*>(this->fp)->insertNode4PreserveFp(
                            this, this->fp_ref, key[fp_depth], newNode);
                        break;
                    case NodeType16:
                        static_cast<Node16*>(this->fp)->insertNode16PreserveFp(
                            this, this->fp_ref, key[fp_depth], newNode);
                        break;
                    case NodeType48:
                        static_cast<Node48*>(this->fp)->insertNode48PreserveFp(
                            this, this->fp_ref, key[fp_depth], newNode);
                        break;
                    case NodeType256:
                        static_cast<Node256*>(this->fp)->insertNode256(
                            this, this->fp_ref, key[fp_depth], newNode);
                        break;
                }
                return;
            }
            // Else, we call the recursive function and let it handle leaf expansion
            // or prefix mismatch if there is one. If not, it will directly insert
            // into the fp
            else {
                insert_recursive_preserve_fp(
                    this->fp, this->fp_ref, key, fp_depth, value, maxPrefixLength);
                return;
            }
        } else if (type == KeyType::BRIDGE) {
            if (this->reset_counter != RESET_COUNTER_INIT)
                this->reset_counter = RESET_COUNTER_INIT;
            this->fp_path = {this->root};
            this->fp_path_length = 1;
            insert_recursive_change_fp(
                this->root, &this->root, key, 0, value, maxPrefixLength);
            return;
        } else { // OTHER (both greater and less)
            if (this->reset_counter == 0) {
                this->reset_counter = RESET_COUNTER_INIT;
                this->fp_path = {this->root};
                this->fp_path_length = 1;
                insert_recursive_change_fp(
                    this->root, &this->root, key, 0, value, maxPrefixLength);
            } else {
                this->reset_counter--;
                insert_recursive_preserve_fp(
                    this->root, &this->root, key, 0, value, maxPrefixLength);
            }
            return;
        }
    }

    KeyType getKeyType(uint8_t key[]) {
        uint32_t leafUpper = (uint32_t)(getLeafValue(this->fp_leaf) >> 8) & 0xFFFFFF;
        uint32_t keyUpper  = ((uint32_t)key[0] << 16) | ((uint32_t)key[1] << 8) | key[2];

        if (keyUpper == leafUpper)
            return KeyType::FP_INSERT;
        if (((keyUpper + 1) & 0xFFFFFF) == leafUpper ||
            ((leafUpper + 1) & 0xFFFFFF) == keyUpper)
            return KeyType::BRIDGE;
        return KeyType::OTHER;
    }
};

}  // namespace ART