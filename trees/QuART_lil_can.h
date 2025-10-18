#pragma once

#include "../ART.h"
#include "../ArtNode.h"

namespace ART {

class QuART_lil_can : public ART {
   public:
    QuART_lil_can() : ART() {}

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
                    QuART_lil_can::insert_recursive_change_fp(
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
            QuART_lil_can::insert_recursive_change_fp(
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
                    static_cast<Node4*>(this->fp)->lilCanInsertNode4PreserveFp(
                        this, this->fp_ref, key[fp_depth], newNode);
                    break;
                case NodeType16:
                    static_cast<Node16*>(this->fp)->lilCanInsertNode16PreserveFp(
                        this, this->fp_ref, key[fp_depth], newNode);
                    break;
                case NodeType48:
                    static_cast<Node48*>(this->fp)->lilCanInsertNode48PreserveFp(
                        this, this->fp_ref, key[fp_depth], newNode);
                    break;
                case NodeType256:
                    static_cast<Node256*>(this->fp)->insertNode256(
                        this, this->fp_ref, key[fp_depth], newNode);
                    break;
            }
            return;
        } else {
            QuART_lil_can::insert_recursive_change_fp(
                this->fp, this->fp_ref, key, fp_depth, value,
                maxPrefixLength);
            return;
        }
    }

   private: 
    /* Recursive insert function that changes fp_leaf value */
    void insert_recursive_change_fp(ArtNode* node, ArtNode** nodeRef,
                                    uint8_t key[], unsigned depth,
                                    uintptr_t value, unsigned maxKeyLength) {
        // Insert the leaf
        if (node == NULL) {
            *nodeRef = makeLeaf(value);
            // Adjust fp parameters
            this->fp_leaf = *nodeRef;
            this->fp = *nodeRef;
            this->fp_ref = nodeRef;
            this->fp_depth = 0;
            return;
        }

        // If leaf expansion is needed
        if (isLeaf(node)) {
            // Replace leaf with Node4 and store both leaves in it
            uint8_t existingKey[maxKeyLength];
            loadKey(getLeafValue(node), existingKey);
            unsigned newPrefixLength = 0;
            while (existingKey[depth + newPrefixLength] ==
                   key[depth + newPrefixLength])
                newPrefixLength++;

            Node4* newNode = new Node4();
            newNode->prefixLength = newPrefixLength;
            memcpy(newNode->prefix, key + depth,
                   min(newPrefixLength, maxPrefixLength));
            *nodeRef = newNode;

            // Adjust fp parameters
            this->fp_path[this->fp_path_length - 1] = newNode;
            this->fp_depth = depth + newPrefixLength;

            newNode->insertNode4(this, nodeRef,
                                 existingKey[depth + newPrefixLength], node);
            newNode->lilCanInsertNode4ChangeFp(
                this, nodeRef, key[depth + newPrefixLength], makeLeaf(value));
            return;
        }

        // Handle prefix of inner node
        if (node->prefixLength) {
            unsigned mismatchPos =
                prefixMismatch(node, key, depth, maxKeyLength);
            if (mismatchPos != node->prefixLength) {
                // Prefix differs, create new node
                Node4* newNode = new Node4();
                *nodeRef = newNode;
                newNode->prefixLength = mismatchPos;
                memcpy(newNode->prefix, node->prefix,
                       min(mismatchPos, maxPrefixLength));
                // Break up prefix
                if (node->prefixLength < maxPrefixLength) {
                    // In all cases, newNode should be added to fp_path
                    fp_path[fp_path_length - 1] = newNode;
                    newNode->insertNode4(this, nodeRef,
                                         node->prefix[mismatchPos], node);
                    node->prefixLength -= (mismatchPos + 1);
                    memmove(node->prefix, node->prefix + mismatchPos + 1,
                            min(node->prefixLength, maxPrefixLength));
                } else {
                    node->prefixLength -= (mismatchPos + 1);
                    uint8_t minKey[maxKeyLength];
                    loadKey(getLeafValue(minimum(node)), minKey);
                    // In all cases, newNode should be added to fp_path
                    fp_path[fp_path_length - 1] = newNode;
                    newNode->insertNode4(this, nodeRef,
                                         minKey[depth + mismatchPos], node);
                    memmove(node->prefix, minKey + depth + mismatchPos + 1,
                            min(node->prefixLength, maxPrefixLength));
                }
                // Adjust fp_depth
                this->fp_depth = depth;
                newNode->lilCanInsertNode4ChangeFp(
                    this, nodeRef, key[depth + mismatchPos], makeLeaf(value));
                return;
            }
            depth += node->prefixLength;
        }

        // Recurse
        ArtNode** child = findChild(node, key[depth]);
        if (*child) {
            fp_path[fp_path_length] =
                *child;        // add the node to the array before recursion
            fp_path_length++;  // increase the size of the array
            insert_recursive_change_fp(*child, child, key, depth + 1, value,
                                       maxKeyLength);
            return;
        }

        // Insert leaf into inner node
        ArtNode* newNode = makeLeaf(value);
        this->fp_depth = depth - node->prefixLength;
        switch (node->type) {
            case NodeType4:
                static_cast<Node4*>(node)->lilCanInsertNode4ChangeFp(
                    this, nodeRef, key[depth], newNode);
                break;
            case NodeType16:
                static_cast<Node16*>(node)->lilCanInsertNode16ChangeFp(
                    this, nodeRef, key[depth], newNode);
                break;
            case NodeType48:
                static_cast<Node48*>(node)->lilCanInsertNode48ChangeFp(
                    this, nodeRef, key[depth], newNode);
                break;
            case NodeType256:
                static_cast<Node256*>(node)->lilCanInsertNode256ChangeFp(
                    this, nodeRef, key[depth], newNode);
                break;
        }
    }

};

}  // namespace ART