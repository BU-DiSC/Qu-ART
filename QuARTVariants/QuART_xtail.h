#pragma once

#include "ART.h"
#include "ArtNode.h"
#include "QuART_tail.h"

namespace ART {

    class QuART_xtail : public ART {
        public:
            QuART_xtail() : ART() {}

            void insert(uint8_t key[], uintptr_t value) {
                // Check if we can tail insert
                ArtNode* root = this->root;
                // Check if the root is not null and is not a leaf
                if (root != nullptr && !isLeaf(root)) {
                    int leafValue = getLeafValue(this->fp_leaf);
                    // For each byte in the key excluding the last byte,
                    // check if it matches the corresponding byte in the leaf value
                    // If any byte does not match, set can_tail_insert to false
                    for (size_t i = 0; i < maxPrefixLength - 1; i++) {
                        uint8_t leafByte = (leafValue >> (8 * (maxPrefixLength - 1 - i))) & 0xFF;
                        if (key[i] == leafByte) {
                            continue;
                        }
                        else if (key[i] < leafByte) {
                            // If the key is less than the leaf value, we do insert without 
                            // tracking the path, as this will never be the new fp path. We only
                            // update the current fp information if it changes. 
                            this->counter1++;
                            QuART_xtail::insert_recursive_only_update_fp(this, this->root, &this->root, key, 0, value, maxPrefixLength);
                            return;
                        }
                        else { 
                            // If the key is greater than the leaf value, we do tail insert with 
                            // tracking the path and updating fp information in the end
                            this->fp_path = {this->root};
                            this->fp_path_length = 1;
                            this->counter2++;
                            QuART_xtail::insert_recursive_always_change_fp(this, this->root, &this->root, key, 0, value, maxPrefixLength);
                            return;
                        }
                    }                    
                }
                else {
                    // If the root is null or is a leaf, we cannot tail insert
                    this->fp_path = {this->root};
                    this->fp_path_length = 1;
                    QuART_xtail::insert_recursive_always_change_fp(this, this->root, &this->root, key, 0, value, maxPrefixLength);
                    return;
                }                   
                
                if (this->fp_depth == maxPrefixLength - 1) {
                    this->counter3++;
                    // Insert leaf into fp
                    ArtNode* newNode = makeLeaf(value);
                    switch (this->fp->type) {
                        case NodeType4:
                            static_cast<Node4*>(this->fp)->insertNode4OnlyUpdateFp(
                                this, this->fp_ref, key[fp_depth], newNode);
                            break;
                        case NodeType16:
                            static_cast<Node16*>(this->fp)->insertNode16OnlyUpdateFp(
                                this, this->fp_ref, key[fp_depth], newNode);
                            break;
                        case NodeType48:
                            static_cast<Node48*>(this->fp)->insertNode48OnlyUpdateFp(
                                this, this->fp_ref, key[fp_depth], newNode);
                            break;
                        case NodeType256:
                            static_cast<Node256*>(this->fp)->insertNode256OnlyUpdateFp(
                                this, this->fp_ref, key[fp_depth], newNode);
                            break;
                    }
                    return;
                } else {
                    this->counter4++;
                    QuART_xtail::insert_recursive_only_update_fp(
                        this, this->fp, this->fp_ref, key, fp_depth, value,
                        maxPrefixLength);
                    return;
                }
            }
            
        private:

            void insert_recursive_only_update_fp(ART* tree, ArtNode* node, ArtNode** nodeRef, uint8_t key[], unsigned depth,
                uintptr_t value, unsigned maxKeyLength) {

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
                        this->fp_path[this->fp_path_length] = newNode;
                        this->fp_path_length++;
                        this->fp = newNode;
                        this->fp_ref = nodeRef;
                        this->fp_depth = depth + newPrefixLength;
                    }
                    newNode->insertNode4(this, nodeRef, existingKey[depth + newPrefixLength],
                                node);
                    newNode->insertNode4(this, nodeRef, key[depth + newPrefixLength],
                                makeLeaf(value));
                    return;
                }

                // Handle prefix of inner node
                if (node->prefixLength) {
                    unsigned mismatchPos = prefixMismatch(node, key, depth, maxKeyLength);
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
                            auto it = std::find(fp_path.begin(), fp_path.begin() + fp_path_length, node);
                            if (it != fp_path.begin() + fp_path_length) {
                                // Find the position of node in fp_path
                                size_t pos = std::distance(fp_path.begin(), it);
                                std::copy_backward(fp_path.begin() + pos, fp_path.begin() + fp_path_length, fp_path.begin() + fp_path_length + 1);
                                fp_path[pos] = newNode;
                                fp_path_length++;
                                //tree->fp_depth += node->prefixLength;
                            }
                            newNode->insertNode4(this, nodeRef, node->prefix[mismatchPos], node);
                            node->prefixLength -= (mismatchPos + 1);
                            memmove(node->prefix, node->prefix + mismatchPos + 1,
                                    min(node->prefixLength, maxPrefixLength));
                        } else {
                            node->prefixLength -= (mismatchPos + 1);
                            uint8_t minKey[maxKeyLength];
                            loadKey(getLeafValue(minimum(node)), minKey);
                            // If the nodes that being changed is in fp_path
                            auto it = std::find(fp_path.begin(), fp_path.begin() + fp_path_length, node);
                            if (it != fp_path.begin() + fp_path_length) {
                                // Find the position of node in fp_path
                                size_t pos = std::distance(fp_path.begin(), it);
                                std::copy_backward(fp_path.begin() + pos, fp_path.begin() + fp_path_length, fp_path.begin() + fp_path_length + 1);
                                fp_path[pos] = newNode;
                                fp_path_length++;
                                //tree->fp_depth += node->prefixLength;
                            }
                            newNode->insertNode4(this, nodeRef, minKey[depth + mismatchPos],
                                        node);
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
                    insert_recursive_only_update_fp(tree, *child, child, key, depth + 1, value, maxKeyLength);
                    return;
                }
                
                // Insert leaf into inner node
                ArtNode* newNode = makeLeaf(value);
                switch (node->type) {
                    case NodeType4:
                        static_cast<Node4*>(node)->insertNode4OnlyUpdateFp(this, nodeRef, key[depth], newNode);
                        break;
                    case NodeType16:
                        static_cast<Node16*>(node)->insertNode16OnlyUpdateFp(this, nodeRef, key[depth], newNode);
                        break;
                    case NodeType48:
                        static_cast<Node48*>(node)->insertNode48OnlyUpdateFp(this, nodeRef, key[depth], newNode);
                        break;
                    case NodeType256:
                        static_cast<Node256*>(node)->insertNode256OnlyUpdateFp(this, nodeRef, key[depth], newNode);
                        break;
                }
            }    
            
            void insert_recursive_always_change_fp(ART* tree, ArtNode* node, ArtNode** nodeRef, uint8_t key[], unsigned depth,
                uintptr_t value, unsigned maxKeyLength) {

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

                    newNode->insertNode4(this, nodeRef, existingKey[depth + newPrefixLength],
                                node);
                    newNode->insertNode4AlwaysChangeFp(this, nodeRef, key[depth + newPrefixLength],
                                makeLeaf(value), depth_prev + newPrefixLength);
                    return;
                }

                // Handle prefix of inner node
                if (node->prefixLength) {
                    unsigned mismatchPos = prefixMismatch(node, key, depth, maxKeyLength);
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
                            newNode->insertNode4(this, nodeRef, node->prefix[mismatchPos], node);
                            node->prefixLength -= (mismatchPos + 1);
                            memmove(node->prefix, node->prefix + mismatchPos + 1,
                                    min(node->prefixLength, maxPrefixLength));
                        } else {
                            node->prefixLength -= (mismatchPos + 1);
                            uint8_t minKey[maxKeyLength];
                            loadKey(getLeafValue(minimum(node)), minKey);
                            // In all cases, newNode should be added to fp_path
                            fp_path[fp_path_length - 1] = newNode;    
                            newNode->insertNode4(this, nodeRef, minKey[depth + mismatchPos], node);
                            memmove(node->prefix, minKey + depth + mismatchPos + 1,
                                    min(node->prefixLength, maxPrefixLength));
                        }
                        newNode->insertNode4AlwaysChangeFp(this, nodeRef, key[depth + mismatchPos],
                                    makeLeaf(value), depth_prev);
                        return;
                    }
                    depth += node->prefixLength;
                }

                // Recurse
                ArtNode** child = findChild(node, key[depth]);
                if (*child) {
                    fp_path[fp_path_length] = *child; // add the node to the array before recursion
                    fp_path_length++; // increase the size of the array    
                    insert_recursive_always_change_fp(tree, *child, child, key, depth + 1, value, maxKeyLength);
                    return;
                }
                
                // Insert leaf into inner node
                ArtNode* newNode = makeLeaf(value);
                switch (node->type) {
                    case NodeType4:
                        static_cast<Node4*>(node)->insertNode4AlwaysChangeFp(this, nodeRef, key[depth],
                                    newNode, depth_prev);
                        break;
                    case NodeType16:
                        static_cast<Node16*>(node)->insertNode16AlwaysChangeFp(this, nodeRef, key[depth],
                                    newNode, depth_prev);
                        break;
                    case NodeType48:
                        static_cast<Node48*>(node)->insertNode48AlwaysChangeFp(this, nodeRef, key[depth],
                                    newNode, depth_prev);
                        break;
                    case NodeType256:
                        static_cast<Node256*>(node)->insertNode256AlwaysChangeFp(this, nodeRef, key[depth],
                                    newNode, depth_prev);
                        break;
                }
            }            


    };

} // namespace ART