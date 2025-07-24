#pragma once

#include "ART.h"
#include "ArtNode.h"
#include "QuART_tail.h"

namespace ART {

    class QuART_smarttail : public ART {
        public:
        QuART_smarttail() : ART() {}

            void insert(uint8_t key[], uintptr_t value) {

                //printf("%lu\n", value);
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
                            QuART_smarttail::insert_recursive_only_update_fp(this, this->root, &this->root, key, 0, value, maxPrefixLength);
                            return;
                        }
                        else { 
                            if (i == 0 || i == 1) {
                                if ((leafValue >> 8 * (maxPrefixLength - smartIdx - 1)) & 0xFF == 255) {
                                    printf("tail is changing from value %lu to value %lu\n",
                                        getLeafValue(this->fp_leaf), value);    
                                    QuART_smarttail::insert_recursive_always_change_fp(this, this->root, &this->root, key, 0, value, maxPrefixLength);
                                    // decrement smartIdx carefully
                                    if ((leafValue >> 8 * (maxPrefixLength - smartIdx)) & 0xFF == 255 && smartIdx != 0) {
                                        this->smartIdx--;
                                    }
                                    return;
                                }
                                else {
                                    printf("tail is not changing for %lu\n", value);
                                    QuART_smarttail::insert_recursive_only_update_fp(this, this->root, &this->root, key, 0, value, maxPrefixLength);
                                    return;
                                }
                            }
                            else {
                                printf("tail is changing from value %lu to value %lu\n",
                                    getLeafValue(this->fp_leaf), value);
                                // If the key is greater than the leaf value, we do tail insert with 
                                // tracking the path and updating fp information in the end
                                QuART_smarttail::insert_recursive_always_change_fp(this, this->root, &this->root, key, 0, value, maxPrefixLength);
                                return;
                            }
                        }
                    }                    
                }
                else {
                    // If the root is null or is a leaf, we cannot tail insert
                    QuART_smarttail::insert_recursive_only_update_fp(this, this->root, &this->root, key, 0, value, maxPrefixLength);
                    return;
                }                   
                
                counter1++;

                if (this->fp_depth == maxPrefixLength - 1) {
                    // Insert leaf into fp
                    ArtNode* newNode = makeLeaf(value);
                    switch (this->fp->type) {
                        case NodeType4:
                            static_cast<Node4*>(this->fp)->insertNode4OnlyUpdateFp2(
                                this, this->fp_ref, key[fp_depth], newNode);
                            break;
                        case NodeType16:
                            static_cast<Node16*>(this->fp)->insertNode16OnlyUpdateFp2(
                                this, this->fp_ref, key[fp_depth], newNode);
                            break;
                        case NodeType48:
                            static_cast<Node48*>(this->fp)->insertNode48OnlyUpdateFp2(
                                this, this->fp_ref, key[fp_depth], newNode);
                            printTree();
                            break;
                        case NodeType256:
                            static_cast<Node256*>(this->fp)->insertNode256OnlyUpdateFp2(
                                this, this->fp_ref, key[fp_depth], newNode);
                            break;
                    }
                    return;
                } else {
                    QuART_smarttail::insert_recursive_only_update_fp(
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
                        printf("prefix mismatch at node address: %p\n", (void*)node);
                        // Prefix differs, create new node
                        Node4* newNode = new Node4();
                        *nodeRef = newNode;
                        newNode->prefixLength = mismatchPos;
                        memcpy(newNode->prefix, node->prefix,
                            min(mismatchPos, maxPrefixLength));
                        // Break up prefix
                        if (node->prefixLength < maxPrefixLength) {
                            newNode->insertNode4Smart(this, nodeRef, node->prefix[mismatchPos], node);
                            node->prefixLength -= (mismatchPos + 1);
                            memmove(node->prefix, node->prefix + mismatchPos + 1,
                                    min(node->prefixLength, maxPrefixLength));
                        } else {
                            node->prefixLength -= (mismatchPos + 1);
                            uint8_t minKey[maxKeyLength];
                            loadKey(getLeafValue(minimum(node)), minKey);
                            newNode->insertNode4Smart(this, nodeRef, minKey[depth + mismatchPos],
                                        node);
                            memmove(node->prefix, minKey + depth + mismatchPos + 1,
                                    min(node->prefixLength, maxPrefixLength));
                        }
                        //printf("fp_ref points to at the end %p\n", static_cast<void*>(*tree->fp_ref));
                        //printf("nodeRef points to at the end %p\n", static_cast<void*>(*nodeRef));
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
                        static_cast<Node4*>(node)->insertNode4OnlyUpdateFp2(this, nodeRef, key[depth], newNode);
                        break;
                    case NodeType16:
                        static_cast<Node16*>(node)->insertNode16OnlyUpdateFp2(this, nodeRef, key[depth], newNode);
                        break;
                    case NodeType48:
                        static_cast<Node48*>(node)->insertNode48OnlyUpdateFp2(this, nodeRef, key[depth], newNode);
                        break;
                    case NodeType256:
                        static_cast<Node256*>(node)->insertNode256OnlyUpdateFp2(this, nodeRef, key[depth], newNode);
                        break;
                }
            }    
            
            void insert_recursive_always_change_fp(ART* tree, ArtNode* node, ArtNode** nodeRef, uint8_t key[], unsigned depth,
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

                    this->fp_depth = depth + newPrefixLength;

                    newNode->insertNode4(this, nodeRef, existingKey[depth + newPrefixLength],
                                node);
                    newNode->insertNode4AlwaysChangeFp2(this, nodeRef, key[depth + newPrefixLength],
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
                            newNode->insertNode4(this, nodeRef, node->prefix[mismatchPos], node);
                            node->prefixLength -= (mismatchPos + 1);
                            memmove(node->prefix, node->prefix + mismatchPos + 1,
                                    min(node->prefixLength, maxPrefixLength));
                        } else {
                            node->prefixLength -= (mismatchPos + 1);
                            uint8_t minKey[maxKeyLength];
                            loadKey(getLeafValue(minimum(node)), minKey);
                            newNode->insertNode4(this, nodeRef, minKey[depth + mismatchPos], node);
                            memmove(node->prefix, minKey + depth + mismatchPos + 1,
                                    min(node->prefixLength, maxPrefixLength));
                        }
                        this->fp_depth = depth;
                        newNode->insertNode4AlwaysChangeFp2(this, nodeRef, key[depth + mismatchPos],
                                    makeLeaf(value));
                        return;
                    }
                    depth += node->prefixLength;
                }

                // Recurse
                ArtNode** child = findChild(node, key[depth]);
                if (*child) {
                    insert_recursive_always_change_fp(tree, *child, child, key, depth + 1, value, maxKeyLength);
                    return;
                }
                
                // Insert leaf into inner node
                ArtNode* newNode = makeLeaf(value);
                this->fp_depth = depth - node->prefixLength;
                switch (node->type) {
                    case NodeType4:
                        static_cast<Node4*>(node)->insertNode4AlwaysChangeFp2(this, nodeRef, key[depth],
                                    newNode);
                        break;
                    case NodeType16:
                        static_cast<Node16*>(node)->insertNode16AlwaysChangeFp2(this, nodeRef, key[depth],
                                    newNode);
                        break;
                    case NodeType48:
                        static_cast<Node48*>(node)->insertNode48AlwaysChangeFp2(this, nodeRef, key[depth],
                                    newNode);
                        break;
                    case NodeType256:
                        static_cast<Node256*>(node)->insertNode256AlwaysChangeFp2(this, nodeRef, key[depth],
                                    newNode);
                        break;
                }
            }            


    };

} // namespace ART