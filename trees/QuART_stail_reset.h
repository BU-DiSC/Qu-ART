#pragma once

#include "../ART.h"
#include "../ArtNode.h"
#include "QuART_stail.h"

namespace ART {

class QuART_stail_reset : public QuART_stail {
   private:
    int reset_counter;

   public:
    QuART_stail_reset() : QuART_stail(), reset_counter(300) {}

    void insert(uint8_t key[], uintptr_t value) {
        /* Check if we can tail insert */

        ArtNode* root = this->root;
        
        // If the root is null (i = 0), we will insert and change fp since
        // keys[0] = 1 in all cases
        if (root == nullptr) {
            fp_change_count++;
            QuART_stail::insert_recursive_change_fp(
                this->root, &this->root, key, 0, value, maxPrefixLength);
            return;
        }

        // Store leafValue, it will be used a lot
        int leafValue = getLeafValue(this->fp_leaf);

        // For each byte in the key excluding the last byte,
        // check if it matches the corresponding byte in the leaf value
        for (size_t i = 0; i < maxPrefixLength - 1; i++) {
            // Find the byte of leaf in the i'th position (i = [0, 1, 2])
            uint8_t leafByte =
                (leafValue >> (8 * (maxPrefixLength - 1 - i))) & 0xFF;

            // If the bytes match, continue to next byte
            if (key[i] == leafByte) {
                continue;
            }
            // If the key byte is less than the leaf value, we do insert without
            // tracking the path, as this will never be the new fp path. We
            // only update the current fp information if it changes.
            else if (key[i] < leafByte) {
                QuART_stail::insert_recursive_preserve_fp(
                    this->root, &this->root, key, 0, value, maxPrefixLength);
                return;
            }
            // Key byte is greater than leaf byte
            else {
                // If we are at the first byte
                if (i == 0) {
                    // If the key is a bridge value, change fp
                    if ((key[0] == leafByte + 1) && (key[1] == 0) &&
                        (key[2] == 0) && ((leafValue >> 8 * 2) & 0xFF) == 255 &&
                        ((leafValue >> 8) & 0xFF) == 255) {
                        this->fp_path = {this->root};
                        this->fp_path_length = 1;
                        fp_change_count++;
                        QuART_stail::insert_recursive_change_fp(
                            this->root, &this->root, key, 0, value,
                            maxPrefixLength);
                        return;
                    }
                    // If it is not a bridge value and counter ended, force fp change
                    else if (this->reset_counter == 0) {
                        this->reset_counter = 300; // reset counter
                        fp_change_count++;
                        this->insert_recursive_change_fp(
                            this->root, &this->root, key, 0, value,
                            maxPrefixLength);
                        return;
                    }
                    // If it is not a bridge value, insert without changing
                    else {
                        this->reset_counter--; // decrement counter
                        QuART_stail::insert_recursive_preserve_fp(
                            this->root, &this->root, key, 0, value,
                            maxPrefixLength);
                        return;
                    }
                }
                // If we are at the second byte
                else if (i == 1) {
                    // If the key is a bridge value, change fp
                    if ((key[1] == leafByte + 1) && (key[2] == 0) &&
                        (key[0] == ((leafValue >> 8 * 3) & 0xFF)) &&
                        ((leafValue >> 8) & 0xFF) == 255) {
                        this->fp_path = {this->root};
                        this->fp_path_length = 1;
                        fp_change_count++;
                        QuART_stail::insert_recursive_change_fp(
                            this->root, &this->root, key, 0, value,
                            maxPrefixLength);
                        return;
                    }
                    // If it is not a bridge value and counter ended, force fp change
                    else if (this->reset_counter == 0) {
                        this->reset_counter = 300; // reset counter
                        fp_change_count++;
                        this->insert_recursive_change_fp(
                            this->root, &this->root, key, 0, value,
                            maxPrefixLength);
                        return;
                    }
                    // If it is not a bridge value, insert without changing
                    else {
                        this->reset_counter--; // decrement counter
                        QuART_stail::insert_recursive_preserve_fp(
                            this->root, &this->root, key, 0, value,
                            maxPrefixLength);
                        return;
                    }
                }
                // If we are at the third byte
                else {
                    // If the key is a bridge value, change fp
                    if ((key[2] == leafByte + 1) &&
                        (key[0] == ((leafValue >> 8 * 3) & 0xFF)) &&
                        (key[1] == ((leafValue >> 8 * 2) & 0xFF))) {
                        this->fp_path = {this->root};
                        this->fp_path_length = 1;
                        fp_change_count++;
                        QuART_stail::insert_recursive_change_fp(
                            this->root, &this->root, key, 0, value,
                            maxPrefixLength);
                        return;
                    }
                    // If it is not a bridge value and counter ended, force fp change
                    else if (this->reset_counter == 0) {
                        this->reset_counter = 300; // reset counter
                        fp_change_count++;
                        this->insert_recursive_change_fp(
                            this->root, &this->root, key, 0, value,
                            maxPrefixLength);
                        return;
                    }
                    // If it is not a bridge value, insert without changing
                    else {
                        this->reset_counter--; // decrement counter
                        QuART_stail::insert_recursive_preserve_fp(
                            this->root, &this->root, key, 0, value,
                            maxPrefixLength);
                        return;
                    }
                }
            }
        }

        /* If the algorithm reaches here, it means that fp insert will happen */

        // If depth is at maxPrefixLength - 1, we do not need to worry about
        // leaf expansion of prefix mismatch, we can directly insert the new
        // leaf into fp node
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
    }
};

}  // namespace ART