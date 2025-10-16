#pragma once

#include "../ART.h"
#include "../ArtNode.h"

namespace ART {

class QuART_lil_can : public QuART_stail {
   public:
    QuART_lil_can() : QuART_stail() {}

    void insert(uint8_t key[], uintptr_t value) {
        // Check if we can lil insert
        ArtNode* root = this->root;
        // Check if the root is not null and is not a leaf
        if (root != nullptr && !isLeaf(root)) {
            int leafValue = getLeafValue(this->fp_leaf);
            // For each byte in the key excluding the last byte,
            // check if it matches the corresponding byte in the leaf value
            // If any byte does not match, set can_lil_insert to false
            for (size_t i = 0; i < maxPrefixLength - 1; ++i) {
                uint8_t leafByte =
                    (leafValue >> (8 * (maxPrefixLength - 1 - i))) & 0xFF;
                if (leafByte != key[i]) {
                    //counter2++; 
                    // If the key defers from leafByte earlier, we lil insert
                    // from root
                    this->fp_path = {this->root};
                    this->fp_path_length = 1;
                    QuART_stail::insert_recursive_change_fp(
                        this->root, &this->root, key, 0, value,
                        maxPrefixLength);
                    return;
                }
            }
        } else {
            //counter2++;
            // If the root is null or is a leaf, we cannot lil insert
            fp_path = {this->root};
            fp_path_length = 1;
            QuART_stail::insert_recursive_change_fp(
                this->root, &this->root, key, 0, value, maxPrefixLength);
            return;
        }

        /*
        // We reached the last byte of the key, we can lil insert
        printf(
            "%lu doing lil insert for value: %lu, value on leaf node was: "
            "%lu\n",
            fp_depth, value, getLeafValue(this->fp_leaf));
        */

        //counter1++;

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
        } else {
            QuART_stail::insert_recursive_change_fp(
                this->fp, this->fp_ref, key, fp_depth, value,
                maxPrefixLength);
            return;
        }
    }

};

}  // namespace ART