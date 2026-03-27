#pragma once

#include "../ART.h"
#include "../ArtNode.h"
#include "QuART_stail.h"

enum class KeyType { FP_INSERT, BRIDGE, OTHER };

namespace ART {

class QuART_stail_reset_2 : public QuART_stail {
   private:
    int reset_counter;

   public:
    QuART_stail_reset_2() : QuART_stail(), reset_counter(300) {}

    void insert(uint8_t key[], uintptr_t value) {
        /* Check if we can tail insert */

        ArtNode* root = this->root;
        
        // If the root is null (i = 0), we will insert and change fp since
        // keys[0] = 1 in all cases
        if (root == nullptr) {
            QuART_stail::insert_recursive_change_fp(
                this->root, &this->root, key, 0, value, maxPrefixLength);
            return;
        }

        KeyType type = getKeyType(key);

        if (type == KeyType::FP_INSERT) {
            this->reset_counter = 300;
            if (this->fp_depth == maxPrefixLength - 1) {
                // Insert leaf into fp
                ArtNode* newNode = makeLeaf(value);
                switch (this->fp->type) {
                    case NodeType4:
                        static_cast<Node4*>(this->fp)->stailInsertNode4PreserveFp(
                            this, this->fp_ref, key[fp_depth], newNode);
                        break;
                    case NodeType16:
                        static_cast<Node16*>(this->fp)->stailInsertNode16PreserveFp(
                            this, this->fp_ref, key[fp_depth], newNode);
                        break;
                    case NodeType48:
                        static_cast<Node48*>(this->fp)->stailInsertNode48PreserveFp(
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
                QuART_stail::insert_recursive_preserve_fp(
                    this->fp, this->fp_ref, key, fp_depth, value, maxPrefixLength);
                return;
            }
        } else if (type == KeyType::BRIDGE) {
            this->reset_counter = 300;
            this->fp_path = {this->root};
            this->fp_path_length = 1;
            QuART_stail::insert_recursive_change_fp(
                this->root, &this->root, key, 0, value, maxPrefixLength);
            return;
        } else { // OTHER (both greater and less)
            if (this->reset_counter == 0) {
                this->reset_counter = 300;
                this->fp_path = {this->root};
                this->fp_path_length = 1;
                QuART_stail::insert_recursive_change_fp(
                    this->root, &this->root, key, 0, value, maxPrefixLength);
            } else {
                this->reset_counter--;
                QuART_stail::insert_recursive_preserve_fp(
                    this->root, &this->root, key, 0, value, maxPrefixLength);
            }
            return;
        }
    }

    KeyType getKeyType(uint8_t key[]) {
        int leafValue = getLeafValue(this->fp_leaf);
        uint8_t leaf[3];
        leaf[0] = (leafValue >> 24) & 0xFF;
        leaf[1] = (leafValue >> 16) & 0xFF;
        leaf[2] = (leafValue >> 8)  & 0xFF;

        // All 3 upper bytes match → fp insert
        if (key[0] == leaf[0] && key[1] == leaf[1] && key[2] == leaf[2])
            return KeyType::FP_INSERT;

        // bytes 0,1 equal, byte 2 adjacent in either direction
        if (key[0] == leaf[0] && key[1] == leaf[1]) {
            if (key[2] == (uint8_t)(leaf[2] + 1) || leaf[2] == (uint8_t)(key[2] + 1))
                return KeyType::BRIDGE;
        }

        // byte 0 equal, byte 1 carries over byte 2 boundary
        if (key[0] == leaf[0]) {
            // ascending: n 255 → n+1 0
            if (key[1] == (uint8_t)(leaf[1] + 1) && key[2] == 0   && leaf[2] == 255)
                return KeyType::BRIDGE;
            // descending: n+1 0 → n 255
            if (leaf[1] == (uint8_t)(key[1] + 1) && leaf[2] == 0  && key[2] == 255)
                return KeyType::BRIDGE;
        }

        // byte 0 carries over bytes 1,2 boundary
        // ascending: n 255 255 → n+1 0 0
        if (key[0] == (uint8_t)(leaf[0] + 1) && key[1] == 0  && key[2] == 0  && leaf[1] == 255 && leaf[2] == 255)
            return KeyType::BRIDGE;
        // descending: n+1 0 0 → n 255 255
        if (leaf[0] == (uint8_t)(key[0] + 1) && leaf[1] == 0 && leaf[2] == 0 && key[1] == 255  && key[2] == 255)
            return KeyType::BRIDGE;

        return KeyType::OTHER;
    }
};

}  // namespace ART