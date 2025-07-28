#pragma once

#include "ART.h"
#include "ArtNode.h"
#include "QuART_tail.h"

namespace ART {

    class QuART_stail : public ART {
        public:
            QuART_stail() : ART() {}

            void insert(uint8_t key[], uintptr_t value) {
                // Check if we can tail insert
                ArtNode* root = this->root;
                // Check if the root is not null and is not a leaf
                if (root != nullptr && !isLeaf(root)) {
                    int leafValue = getLeafValue(this->fp_leaf);
                    // For each byte in the key excluding the last byte,
                    // check if it matchmes the corresponding byte in the leaf value
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
                            //counter3++;
                            QuART_stail::insert_recursive_only_update_fp(this, this->root, &this->root, key, 0, value, maxPrefixLength);
                            return;
                        }
                        else { 
                            /*
                            int lB = (leafValue >> (8 * (maxPrefixLength - 1 - smartIdx))) & 0xFF;
                            if (lB == 255) {
                                printf("tail is changing for %lu\n", value);
                                counter2++;
                                smartIdx--;
                                this->fp_path = {this->root};
                                this->fp_path_length = 1;
                                QuART_stail::insert_recursive_always_change_fp(this, this->root, &this->root, key, 0, value, maxPrefixLength);
                                return;
                            }
                            else if (lB + 1 == key[smartIdx]) {
                                if (smartIdx == 2) {
                                    if (key[1] == 0 && key[0] == 0) {
                                        printf("tail is changing for %lu\n", value);
                                        counter2++;
                                        this->fp_path = {this->root};
                                        this->fp_path_length = 1;
                                        QuART_stail::insert_recursive_always_change_fp(this, this->root, &this->root, key, 0, value, maxPrefixLength);
                                        return;
                                    }
                                }
                                else if (smartIdx == 1) { 
                                    if (key[0] == 0) {
                                        printf("tail is changing for %lu\n", value);
                                        counter2++;
                                        this->fp_path = {this->root};
                                        this->fp_path_length = 1;
                                        QuART_stail::insert_recursive_always_change_fp(this, this->root, &this->root, key, 0, value, maxPrefixLength);
                                        return;                                   
                                    }
                                }
                                counter3++;
                                //printf("tail is not changing for %lu\n", value);
                                QuART_stail::insert_recursive_only_update_fp(this, this->root, &this->root, key, 0, value, maxPrefixLength);
                                return;
                            }
                            else {
                                counter3++;
                                //printf("tail is not changing for %lu\n", value);
                                QuART_stail::insert_recursive_only_update_fp(this, this->root, &this->root, key, 0, value, maxPrefixLength);
                                return;
                            }
                            */
                            if (i == 0) {
                                if ((key[0] == leafByte + 1) && (key[1] == 0) && (key[2] == 0) && 
                                    ((leafValue >> 8 * 2) & 0xFF) == 255 && ((leafValue >> 8) & 0xFF) == 255)  {
                                        //counter2++;
                                        //printf("tail is changing for %lu, from %lu\n", value, leafValue);
                                        this->fp_path = {this->root};
                                        this->fp_path_length = 1;                    
                                        QuART_stail::insert_recursive_always_change_fp(this, this->root, &this->root, key, 0, value, maxPrefixLength);
                                        return;                                   
                                } 
                                else {
                                    //counter3++;
                                    QuART_stail::insert_recursive_only_update_fp(this, this->root, &this->root, key, 0, value, maxPrefixLength);
                                    return;
                                }
                            }
                            else if (i == 1) {
                                if ((key[1] == leafByte + 1) && (key[2] == 0) && (key[0] == ((leafValue >> 8*3) & 0xFF)) && 
                                    ((leafValue >> 8) & 0xFF) == 255)  {
                                        //counter2++;
                                        //printf("tail is changing for %lu, from %lu\n", value, leafValue);
                                        this->fp_path = {this->root};
                                        this->fp_path_length = 1;                    
                                        QuART_stail::insert_recursive_always_change_fp(this, this->root, &this->root, key, 0, value, maxPrefixLength);
                                        return;                                   
                                } 
                                else {
                                    //counter3++;
                                    QuART_stail::insert_recursive_only_update_fp(this, this->root, &this->root, key, 0, value, maxPrefixLength);
                                    return;
                                }
                            }
                            else {
                                if ((key[2] == leafByte + 1) && (key[0] == ((leafValue >> 8*3) & 0xFF)) && (key[1] == ((leafValue >> 8*2) & 0xFF))) {
                                    //counter2++;
                                    //printf("tail is changing for %lu, from %lu\n", value, leafValue);
                                    this->fp_path = {this->root};
                                    this->fp_path_length = 1;                
                                    QuART_stail::insert_recursive_always_change_fp(this, this->root, &this->root, key, 0, value, maxPrefixLength);
                                    return;                                   
                                } 
                                else {
                                    //counter3++;
                                    QuART_stail::insert_recursive_only_update_fp(this, this->root, &this->root, key, 0, value, maxPrefixLength);
                                    return;
                                }
                            }
                        }
                    }                    
                }
                else {
                    //counter2++;
                    // If the root is null or is a leaf, we cannot tail insert
                    //printf("tail is changing for %lu, from %lu\n", value, getLeafValue(this->fp_leaf));
                    this->fp_path = {this->root};
                    this->fp_path_length = 1;
                    QuART_stail::insert_recursive_always_change_fp(this, this->root, &this->root, key, 0, value, maxPrefixLength);
                    return;
                }                   

                //counter1++;
                
                if (this->fp_depth == maxPrefixLength - 1) {
                    // Insert leaf into fp
                    ArtNode* newNode = makeLeaf(value);
                    switch (this->fp->type) {
                        case NodeType4:
                            static_cast<Node4*>(this->fp)->insertNode4OnlyUpdateFpS(
                                this, this->fp_ref, key[fp_depth], newNode);
                            break;
                        case NodeType16:
                            static_cast<Node16*>(this->fp)->insertNode16OnlyUpdateFpS(
                                this, this->fp_ref, key[fp_depth], newNode);
                            break;
                        case NodeType48:
                            static_cast<Node48*>(this->fp)->insertNode48OnlyUpdateFpS(
                                this, this->fp_ref, key[fp_depth], newNode);
                            break;
                        case NodeType256:
                            static_cast<Node256*>(this->fp)->insertNode256OnlyUpdateFp(
                                this, this->fp_ref, key[fp_depth], newNode);
                            break;
                    }
                    return;
                } else {
                    QuART_stail::insert_recursive_only_update_fp(
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
                    //printf("prefix mismatch at node address: %p\n", (void*)node);
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
                    static_cast<Node4*>(node)->insertNode4OnlyUpdateFpS(this, nodeRef, key[depth], newNode);
                    break;
                case NodeType16:
                    static_cast<Node16*>(node)->insertNode16OnlyUpdateFpS(this, nodeRef, key[depth], newNode);
                    break;
                case NodeType48:
                    static_cast<Node48*>(node)->insertNode48OnlyUpdateFpS(this, nodeRef, key[depth], newNode);
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