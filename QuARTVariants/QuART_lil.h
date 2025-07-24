#pragma once

#include "ART.h"
#include "ArtNode.h"

namespace ART {

class QuART_lil : public ART {
   public:
    QuART_lil() : ART() {}

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
                    counter1++;
                    // If the key defers from leafByte earlier, we lil insert
                    // from root
                    this->fp_path = {this->root};
                    this->fp_path_length = 1;
                    QuART_lil::insert_recursive_always_change_fp(
                        this, this->root, &this->root, key, 0, value,
                        maxPrefixLength);
                    return;
                }
            }
        } else {
            // If the root is null or is a leaf, we cannot lil insert
            fp_path = {this->root};
            fp_path_length = 1;
            QuART_lil::insert_recursive_always_change_fp(
                this, this->root, &this->root, key, 0, value, maxPrefixLength);
            return;
        }

        /*
        // We reached the last byte of the key, we can lil insert
        printf(
            "%lu doing lil insert for value: %lu, value on leaf node was: "
            "%lu\n",
            fp_depth, value, getLeafValue(this->fp_leaf));
        */

        if (fp_depth == maxPrefixLength - 1) {
            counter2++;
            // Insert leaf into fp
            ArtNode* newNode = makeLeaf(value);
            switch (this->fp->type) {
                case NodeType4:
                    static_cast<Node4*>(this->fp)->insertNode4AlwaysChangeFp(
                        this, this->fp_ref, key[fp_depth], newNode, fp_depth);
                    break;
                case NodeType16:
                    static_cast<Node16*>(this->fp)->insertNode16AlwaysChangeFp(
                        this, this->fp_ref, key[fp_depth], newNode, fp_depth);
                    break;
                case NodeType48:
                    static_cast<Node48*>(this->fp)->insertNode48AlwaysChangeFp(
                        this, this->fp_ref, key[fp_depth], newNode, fp_depth);
                    break;
                case NodeType256:
                    static_cast<Node256*>(this->fp)
                        ->insertNode256AlwaysChangeFp(this, this->fp_ref,
                                                      key[fp_depth], newNode,
                                                      fp_depth);
                    break;
            }
            return;
        } else {
            counter3++;
            QuART_lil::insert_recursive_always_change_fp(
                this, this->fp, this->fp_ref, key, fp_depth, value,
                maxPrefixLength);
            return;
        }
    }

   private:
    void insert_recursive_always_change_fp(ART* tree, ArtNode* node,
                                           ArtNode** nodeRef, uint8_t key[],
                                           unsigned depth, uintptr_t value,
                                           unsigned maxKeyLength) {
        size_t depth_prev = depth;

        // Insert the leaf value into the tree
        if (node == NULL) {
            *nodeRef = makeLeaf(value);
            // Adjust only fp_leaf (fp will still be null)
            tree->fp_leaf = *nodeRef;
            tree->fp_ref = nodeRef;
            return;
        }

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

            fp_path[fp_path_length - 1] = newNode;

            newNode->insertNode4(this, nodeRef,
                                 existingKey[depth + newPrefixLength], node);
            newNode->insertNode4AlwaysChangeFp(this, nodeRef,
                                               key[depth + newPrefixLength],
                                               makeLeaf(value), depth_prev + newPrefixLength);
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
                    // If the nodes that being changed is in fp_path
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
                newNode->insertNode4AlwaysChangeFp(this, nodeRef,
                                                   key[depth + mismatchPos],
                                                   makeLeaf(value), depth);
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
            insert_recursive_always_change_fp(tree, *child, child, key,
                                              depth + 1, value, maxKeyLength);
            return;
        }

        // Insert leaf into inner node
        ArtNode* newNode = makeLeaf(value);
        switch (node->type) {
            case NodeType4:
                static_cast<Node4*>(node)->insertNode4AlwaysChangeFp(
                    this, nodeRef, key[depth], newNode, depth_prev);
                break;
            case NodeType16:
                static_cast<Node16*>(node)->insertNode16AlwaysChangeFp(
                    this, nodeRef, key[depth], newNode, depth_prev);
                break;
            case NodeType48:
                static_cast<Node48*>(node)->insertNode48AlwaysChangeFp(
                    this, nodeRef, key[depth], newNode, depth_prev);
                break;
            case NodeType256:
                static_cast<Node256*>(node)->insertNode256AlwaysChangeFp(
                    this, nodeRef, key[depth], newNode, depth_prev);
                break;
        }
    }
};

}  // namespace ART