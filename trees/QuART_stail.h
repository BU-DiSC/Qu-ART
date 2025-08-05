#pragma once

#include "../ART.h"
#include "../ArtNode.h"

namespace ART {

class QuART_stail : public ART {
   public:
    QuART_stail() : ART() {}

    void insert(uint8_t key[], uintptr_t value) {
        // Check if we can tail insert
        ArtNode* root = this->root;
        // If the root is null (i = 0), we will insert and change fp since
        // keys[0] = 1 in all cases
        if (root == nullptr) {
            QuART_stail::insert_recursive_always_change_fp(
                this, this->root, &this->root, key, 0, value, maxPrefixLength);
            return;
        }
        // If the root is leaf (i = 1), we do a leaf insert and don't update
        // fp_leaf since keys[1] can be an outlier
        else if (isLeaf(root)) {
            QuART_stail::insert_recursive_only_update_fp(
                this, this->root, &this->root, key, 0, value, maxPrefixLength);
            return;
        }
        int leafValue = getLeafValue(this->fp_leaf);
        // For each byte in the key excluding the last byte,
        // check if it matchmes the corresponding byte in the leaf value
        // If any byte does not match, set can_tail_insert to false
        for (size_t i = 0; i < maxPrefixLength - 1; i++) {
            uint8_t leafByte =
                (leafValue >> (8 * (maxPrefixLength - 1 - i))) & 0xFF;
            if (key[i] == leafByte) {
                continue;
            } else if (key[i] < leafByte) {
                // If the key is less than the leaf value, we do insert without
                // tracking the path, as this will never be the new fp path. We
                // only update the current fp information if it changes.
                QuART_stail::insert_recursive_only_update_fp(
                    this, this->root, &this->root, key, 0, value,
                    maxPrefixLength);
                return;
            } else {
                if (i == 0) {
                    if ((key[0] == leafByte + 1) && (key[1] == 0) &&
                        (key[2] == 0) && ((leafValue >> 8 * 2) & 0xFF) == 255 &&
                        ((leafValue >> 8) & 0xFF) == 255) {
                        this->fp_path = {this->root};
                        this->fp_path_length = 1;
                        QuART_stail::insert_recursive_always_change_fp(
                            this, this->root, &this->root, key, 0, value,
                            maxPrefixLength);
                        return;
                    } else {
                        QuART_stail::insert_recursive_only_update_fp(
                            this, this->root, &this->root, key, 0, value,
                            maxPrefixLength);
                        return;
                    }
                } else if (i == 1) {
                    if ((key[1] == leafByte + 1) && (key[2] == 0) &&
                        (key[0] == ((leafValue >> 8 * 3) & 0xFF)) &&
                        ((leafValue >> 8) & 0xFF) == 255) {
                        this->fp_path = {this->root};
                        this->fp_path_length = 1;
                        QuART_stail::insert_recursive_always_change_fp(
                            this, this->root, &this->root, key, 0, value,
                            maxPrefixLength);
                        return;
                    } else {
                        QuART_stail::insert_recursive_only_update_fp(
                            this, this->root, &this->root, key, 0, value,
                            maxPrefixLength);
                        return;
                    }
                } else {
                    if ((key[2] == leafByte + 1) &&
                        (key[0] == ((leafValue >> 8 * 3) & 0xFF)) &&
                        (key[1] == ((leafValue >> 8 * 2) & 0xFF))) {
                        this->fp_path = {this->root};
                        this->fp_path_length = 1;
                        QuART_stail::insert_recursive_always_change_fp(
                            this, this->root, &this->root, key, 0, value,
                            maxPrefixLength);
                        return;
                    } else {
                        QuART_stail::insert_recursive_only_update_fp(
                            this, this->root, &this->root, key, 0, value,
                            maxPrefixLength);
                        return;
                    }
                }
            }
        }

        // If the algorithm reaches here, it means that fp insert will happen.

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
        // or prefix mismatch if there is one
        else {
            QuART_stail::insert_recursive_only_update_fp(
                this, this->fp, this->fp_ref, key, fp_depth, value,
                maxPrefixLength);
            return;
        }
    }

   private:
    void insert_recursive_only_update_fp(ART* tree, ArtNode* node,
                                         ArtNode** nodeRef, uint8_t key[],
                                         unsigned depth, uintptr_t value,
                                         unsigned maxKeyLength) {
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

            // If the changing node was the fp just straight change the node
            if (tree->fp_leaf == node) {
                if (fp != nullptr) {
                    this->fp_depth += fp->prefixLength;
                    this->fp_depth++;
                }
                this->fp_path[this->fp_path_length] = newNode;
                this->fp_path_length++;
                this->fp = newNode;
                this->fp_ref = nodeRef;
            }

            newNode->insertNode4(this, nodeRef,
                                 existingKey[depth + newPrefixLength], node);
            newNode->insertNode4(this, nodeRef, key[depth + newPrefixLength],
                                 makeLeaf(value));
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
                    // If the nodes that being changed is in fp_path
                    auto it = std::find(fp_path.begin(),
                                        fp_path.begin() + fp_path_length, node);
                    if (it != fp_path.begin() + fp_path_length) {
                        // Find the position of node in fp_path
                        size_t pos = std::distance(fp_path.begin(), it);
                        std::copy_backward(
                            fp_path.begin() + pos,
                            fp_path.begin() + fp_path_length,
                            fp_path.begin() + fp_path_length + 1);
                        fp_path[pos] = newNode;
                        fp_path_length++;
                        if (node == this->fp) {
                            this->fp_depth += newNode->prefixLength;
                            this->fp_depth++;
                        }
                    }
                    newNode->stailInsertNode4PreserveFpPrefixExpansion(
                        this, nodeRef, node->prefix[mismatchPos], node);
                    node->prefixLength -= (mismatchPos + 1);
                    memmove(node->prefix, node->prefix + mismatchPos + 1,
                            min(node->prefixLength, maxPrefixLength));
                } else {
                    node->prefixLength -= (mismatchPos + 1);
                    uint8_t minKey[maxKeyLength];
                    loadKey(getLeafValue(minimum(node)), minKey);
                    // If the nodes that being changed is in fp_path
                    auto it = std::find(fp_path.begin(),
                                        fp_path.begin() + fp_path_length, node);
                    if (it != fp_path.begin() + fp_path_length) {
                        // Find the position of node in fp_path
                        size_t pos = std::distance(fp_path.begin(), it);
                        std::copy_backward(
                            fp_path.begin() + pos,
                            fp_path.begin() + fp_path_length,
                            fp_path.begin() + fp_path_length + 1);
                        fp_path[pos] = newNode;
                        fp_path_length++;
                        if (node == this->fp) {
                            this->fp_depth += newNode->prefixLength;
                            this->fp_depth++;
                        }
                    }
                    newNode->stailInsertNode4PreserveFpPrefixExpansion(
                        this, nodeRef, minKey[depth + mismatchPos], node);
                    memmove(node->prefix, minKey + depth + mismatchPos + 1,
                            min(node->prefixLength, maxPrefixLength));
                }
                newNode->insertNode4(this, nodeRef, key[depth + mismatchPos],
                                     makeLeaf(value));
                return;
            }
            depth += node->prefixLength;
        }

        // Recurse
        ArtNode** child = findChild(node, key[depth]);
        if (*child) {
            insert_recursive_only_update_fp(tree, *child, child, key, depth + 1,
                                            value, maxKeyLength);
            return;
        }

        // Insert leaf into inner node
        ArtNode* newNode = makeLeaf(value);
        switch (node->type) {
            case NodeType4:
                static_cast<Node4*>(node)->stailInsertNode4PreserveFp(
                    this, nodeRef, key[depth], newNode);
                break;
            case NodeType16:
                static_cast<Node16*>(node)->stailInsertNode16PreserveFp(
                    this, nodeRef, key[depth], newNode);
                break;
            case NodeType48:
                static_cast<Node48*>(node)->stailInsertNode48PreserveFp(
                    this, nodeRef, key[depth], newNode);
                break;
            case NodeType256:
                static_cast<Node256*>(node)->insertNode256(this, nodeRef,
                                                           key[depth], newNode);
                break;
        }
    }

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
            newNode->stailInsertNode4ChangeFp(
                this, nodeRef, key[depth + newPrefixLength], makeLeaf(value),
                depth_prev + newPrefixLength);
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
                newNode->stailInsertNode4ChangeFp(this, nodeRef,
                                                  key[depth + mismatchPos],
                                                  makeLeaf(value), depth_prev);
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
                static_cast<Node4*>(node)->stailInsertNode4ChangeFp(
                    this, nodeRef, key[depth], newNode, depth_prev);
                break;
            case NodeType16:
                static_cast<Node16*>(node)->stailInsertNode16ChangeFp(
                    this, nodeRef, key[depth], newNode, depth_prev);
                break;
            case NodeType48:
                static_cast<Node48*>(node)->stailInsertNode48ChangeFp(
                    this, nodeRef, key[depth], newNode, depth_prev);
                break;
            case NodeType256:
                static_cast<Node256*>(node)->stailInsertNode256ChangeFp(
                    this, nodeRef, key[depth], newNode, depth_prev);
                break;
        }
    }
};

}  // namespace ART