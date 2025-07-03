#pragma once

#include "ART.h"
#include "ArtNode.h"

namespace ART {

    class QuART_tail : public ART {
        public:

            QuART_tail() : ART() {}

            void insert(uint8_t key[], uintptr_t value) {
                /*
                 * tail insert status map:
                 * 0 -> can tail insert
                 * 1 -> can't tail insert, but key > leaf value
                 * 2 -> can't tail insert  since key < leaf value"
                 */
                // Check if we can tail insert
                ArtNode* root = this->root;
                int tail_insert_status = 0;
                // Check if the root is not null and is not a leaf
                if (root != nullptr && !isLeaf(root)) {
                    int leafValue = getLeafValue(this->fp_leaf);
                    can_tail_insert = true;
                    // For each byte in the key excluding the last byte,
                    // check if it matches the corresponding byte in the leaf value
                    // If any byte does not match, set can_tail_insert to false
                    size_t i = 0;
                    for (size_t i = 0; i < maxPrefixLength - 1; i++) {
                        uint8_t leafByte = (leafValue >> (8 * (maxPrefixLength - 1 - i))) & 0xFF;
                        if (key[i] > leafByte) {
                            // do normal insert with fp path tracking
                            // call the insert here
                        }
                        else if (key[i] < leafByte) {
                            // do the normal insert without fp path tracking
                            // call the insert here
                        }
                    }
                    // buraya gelirse hepsi eşittir
                    uint8_t leafLast = leafValue & 0xFF;
                    if (key[i] > leafLast) {

                    }
                    
                }

                // If we can tail insert, use the fast path
                if (can_tail_insert) {
                    std::array<ArtNode*, maxPrefixLength> temp_fp_path  = fp_path; 
                    size_t temp_fp_path_length = fp_path_length;

                    // Uncomment the following line to print the tail insert debug information
                    //printf("doing tail insert for value: %lu, value on leaf node was: %lu\n", value, getLeafValue(this->fp_leaf));

                    QuART_tail::insert_recursive(this, this->fp, this->fp_ref, 
                            key, fp_depth, value, maxPrefixLength, 
                            temp_fp_path, temp_fp_path_length);
                }
                // If we cannot tail insert, use the regular insert
                else {
                    std::array<ArtNode*, maxPrefixLength> temp_fp_path = {this->root};
                    size_t temp_fp_path_length = 1;
                    QuART_tail::insert_recursive(this, this->root, &this->root, key, 0, value, maxPrefixLength, 
                        temp_fp_path, temp_fp_path_length);
                }
            }
            
            // Method to verify the tail path after each insertion
            bool verifyTailPath() {
                if (this->fp_path_length == 0) {
                    return true;
                }

                ArtNode* current = this->root;
                // Traverse the tree following the fp_path
                for (size_t i = 0; i < this->fp_path_length; i++) {
                    if (i == this->fp_path_length - 1) {
                        if (current == this->fp) {
                            if (getLeafValue(maximum(current)) == getLeafValue(this->fp_leaf)) {
                                //printf("Success: fp_path is correct.\n");
                                return true;
                            } else {
                                printf("Error: fp_leaf mismatch. Expected %lu, got %lu.\n",
                                    getLeafValue(maximum(current)), getLeafValue(this->fp_leaf));
                                return false;
                            }
                        } else {
                            printf("Error: last node in fp_path is not the fp. Expected %p, got %p.\n",
                                static_cast<void*>(current), static_cast<void*>(this->fp));
                            return false;
                        }
                    }

                    // Move to the rightmost child
                    switch (current->type) {
                        case NodeType4: {
                            Node4* node = static_cast<Node4*>(current);
                            if (node->count > 0) {
                                current = node->child[node->count - 1];
                            } else {
                                printf("Error: NodeType4 has no children.\n");
                                return false;
                            }
                            break;
                        }
                        case NodeType16: {
                            Node16* node = static_cast<Node16*>(current);
                            if (node->count > 0) {
                                current = node->child[node->count - 1];
                            } else {
                                printf("Error: NodeType16 has no children.\n");
                                return false;
                            }
                            break;
                        }
                        case NodeType48: {
                            Node48* node = static_cast<Node48*>(current);
                            unsigned pos = 255;
                            while (pos > 0 && node->childIndex[pos] == emptyMarker) pos--;
                            if (node->childIndex[pos] != emptyMarker) {
                                current = node->child[node->childIndex[pos]];
                            } else {
                                printf("Error: NodeType48 has no valid children.\n");
                                return false;
                            }
                            break;
                        }
                        case NodeType256: {
                            Node256* node = static_cast<Node256*>(current);
                            unsigned pos = 255;
                            while (pos > 0 && !node->child[pos]) pos--;
                            if (node->child[pos]) {
                                current = node->child[pos];
                            } else {
                                printf("Error: NodeType256 has no valid children.\n");
                                return false;
                            }
                            break;
                        }
                        default:
                            printf("Error: Unknown node type.\n");
                            return false;
                    }
                }

                // If we exit the loop without returning, the path is incorrect
                printf("Error: fp_path does not lead to the fp.\n");
                return false;
            }
        
        private:
            void insert_recursive(ART* tree, ArtNode* node, ArtNode** nodeRef, uint8_t key[], unsigned depth,
                uintptr_t value, unsigned maxKeyLength, std::array<ArtNode*, maxPrefixLength>& temp_fp_path,
                size_t& temp_fp_path_length) {

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
                    // If the changing node was the fp, push it to the temp_fp_path
                    if (tree->fp_leaf == node) { 
                        temp_fp_path[temp_fp_path_length - 1] = newNode;
                    }
                    newNode->insertNode4(this, nodeRef, existingKey[depth + newPrefixLength],
                                node, temp_fp_path, temp_fp_path_length, depth_prev);
                    newNode->insertNode4(this, nodeRef, key[depth + newPrefixLength],
                                makeLeaf(value), temp_fp_path, temp_fp_path_length, depth_prev);
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
                            // Stores the temp_fp_path and fp_path sizes before operations
                            size_t temp_fp_path_length_old = temp_fp_path_length;
                            size_t fp_path_length_old = tree->fp_path_length;
                            // In all cases, newNode should be added to fp_path
                            temp_fp_path[temp_fp_path_length_old - 1] = newNode;    
                            // If the nodes that being changed is in fp_path
                            if (temp_fp_path_length_old <= fp_path_length_old && tree->fp_path[fp_path_length_old-1] == node) { 
                                // If the new value is less than the current fp_leaf,
                                // restore fp_path to what it before the change with the
                                // newNode added
                                if (value < getLeafValue(tree->fp)) {
                                    // A deep copy of remainder of fp_path 
                                    std::array<ArtNode*, maxPrefixLength> fp_path_remainder;
                                    std::copy(tree->fp_path.begin() + (temp_fp_path_length_old - 1), 
                                    tree->fp_path.begin() + fp_path_length_old, fp_path_remainder.begin());
                                    tree->fp_path = temp_fp_path; // update the fp path
                                    tree->fp_path_length = temp_fp_path_length_old;
                                    // Add the remainder of fp_path to fp_path
                                    for (int i = 0; i < fp_path_length_old - temp_fp_path_length_old + 1; i++) {
                                        tree->fp_path[i + temp_fp_path_length_old] = fp_path_remainder[i];
                                        tree->fp_path_length++;
                                    }
                                }
                            }
                            newNode->insertNode4(this, nodeRef, node->prefix[mismatchPos], node,
                                    temp_fp_path, temp_fp_path_length, depth_prev);
                            node->prefixLength -= (mismatchPos + 1);
                            memmove(node->prefix, node->prefix + mismatchPos + 1,
                                    min(node->prefixLength, maxPrefixLength));
                        } else {
                            node->prefixLength -= (mismatchPos + 1);
                            uint8_t minKey[maxKeyLength];
                            loadKey(getLeafValue(minimum(node)), minKey);
                            // Stores the temp_fp_path and fp_path sizes before operations
                            size_t temp_fp_path_length_old = temp_fp_path_length;
                            size_t fp_path_length_old = tree->fp_path_length;
                            // In all cases, newNode should be added to fp_path
                            temp_fp_path[temp_fp_path_length_old - 1] = newNode;    
                            // If the nodes that being changed is in fp_path
                            if (temp_fp_path_length_old <= fp_path_length_old && tree->fp_path[fp_path_length_old-1] == node) { 
                                // If the new value is less than the current fp_leaf,
                                // restore fp_path to what it before the change with the
                                // newNode added
                                if (value < getLeafValue(tree->fp)) {
                                    // A deep copy of remainder of fp_path 
                                    std::array<ArtNode*, maxPrefixLength> fp_path_remainder;
                                    std::copy(tree->fp_path.begin() + (temp_fp_path_length_old - 1), 
                                    tree->fp_path.begin() + fp_path_length_old, fp_path_remainder.begin());
                                    tree->fp_path = temp_fp_path; // update the fp path
                                    tree->fp_path_length = temp_fp_path_length_old;
                                    // Add the remainder of fp_path to fp_path
                                    for (int i = 0; i < fp_path_length_old - temp_fp_path_length_old + 1; i++) {
                                        tree->fp_path[i + temp_fp_path_length_old] = fp_path_remainder[i];
                                        tree->fp_path_length++;
                                    }
                                }
                            }
                            newNode->insertNode4(this, nodeRef, minKey[depth + mismatchPos],
                                        node, temp_fp_path, temp_fp_path_length, depth_prev);
                            memmove(node->prefix, minKey + depth + mismatchPos + 1,
                                    min(node->prefixLength, maxPrefixLength));
                        }
                        newNode->insertNode4(this, nodeRef, key[depth + mismatchPos],
                                    makeLeaf(value), temp_fp_path, temp_fp_path_length, depth_prev);
                        return;
                    }
                    depth += node->prefixLength;
                }

                // Recurse
                ArtNode** child = findChild(node, key[depth]);
                if (*child) {
                    temp_fp_path[temp_fp_path_length] = *child; // add the node to the array before recursion
                    temp_fp_path_length++; // increase the size of the array    
                    insert_recursive(tree, *child, child, key, depth + 1, value, maxKeyLength, 
                        temp_fp_path, temp_fp_path_length);
                    return;
                }
                
                // Insert leaf into inner node
                ArtNode* newNode = makeLeaf(value);
                switch (node->type) {
                    case NodeType4:
                        static_cast<Node4*>(node)->insertNode4(this, nodeRef, key[depth],
                                    newNode, temp_fp_path, temp_fp_path_length, depth_prev);
                        break;
                    case NodeType16:
                        static_cast<Node16*>(node)->insertNode16(this, nodeRef, key[depth],
                                    newNode, temp_fp_path, temp_fp_path_length, depth_prev);
                        break;
                    case NodeType48:
                        static_cast<Node48*>(node)->insertNode48(this, nodeRef, key[depth],
                                    newNode, temp_fp_path, temp_fp_path_length, depth_prev);
                        break;
                    case NodeType256:
                        static_cast<Node256*>(node)->insertNode256(this, nodeRef, key[depth],
                                    newNode, temp_fp_path, temp_fp_path_length, depth_prev);
                        break;
                }
            }            
    };

} // namespace ART