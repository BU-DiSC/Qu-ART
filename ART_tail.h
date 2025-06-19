#pragma once

#include "ART.h"
#include "ArtNode.h"

namespace ART {

    class ART_tail : public ART {
        public:

            ART_tail() : ART() {}

            void insert(uint8_t key[], uintptr_t value) {
                std::array<ArtNode*, maxPrefixLength> temp_fp_path;
                size_t temp_fp_path_length = 0;
                    if (canTailInsert(key, temp_fp_path, temp_fp_path_length)) {
                        // adjust temp_fp_path and fp_path too
                        printf("can tail insert\n");
                        printf("doing tail insert with\n");
                        printTailPath(temp_fp_path, temp_fp_path_length);
                        ART_tail::insert(this, this->fp, &root, key, 0, value, maxPrefixLength, 
                            temp_fp_path, temp_fp_path_length);
                    }
                    else {
                        printf("normal insert\n");
                        ART_tail::insert(this, root, &root, key, 0, value, maxPrefixLength, 
                            temp_fp_path, temp_fp_path_length);
                    }
                }
            
            
            bool canTailInsert(uint8_t key[], std::array<ArtNode*, maxPrefixLength>& temp_fp_path, 
                size_t& temp_fp_path_length) {
                
                if (this->fp_path_length == 0) {
                    return false; 
                }

                if (getLeafValue(this->fp_leaf) > keyToInt(key)) {
                    return false;
                }


                ArtNode* ptr = this->fp_path[0];
                
                size_t j = 0;

                for (size_t i = 0; i < this->fp_path_length; i++) {
                    if (ptr->prefixLength != 0) {
                        for (j; j < ptr->prefixLength; j++) {
                            if (ptr->prefix[j] != key[i]) {
                                temp_fp_path = {};
                                temp_fp_path_length = 0;
                                return false;
                            }
                        }
                    }

                    if (ptr == this->fp) {
                        return true;
                    }

                    temp_fp_path[temp_fp_path_length] = ptr; // update the fp_path
                    temp_fp_path_length++;

                    printf("node ptr added to temp_fp_path: %p\n", static_cast<void*>(ptr));
                }            
                temp_fp_path = {};
                temp_fp_path_length = 0;

                return false;
            }
            

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
                                    getLeafValue(this->fp_leaf), getLeafValue(maximum(current)));
                                return false;
                            }
                        } else {
                            printf("Error: last node in fp_path is not the fp. Expected %p, got %p.\n",
                                static_cast<void*>(this->fp), static_cast<void*>(current));
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

            void printTree() { 
                printTree(this->root, 0);
            }
        
        private:
            void insert(ART* tree, ArtNode* node, ArtNode** nodeRef, uint8_t key[], unsigned depth,
                uintptr_t value, unsigned maxKeyLength, std::array<ArtNode*, maxPrefixLength> temp_fp_path,
                size_t temp_fp_path_length) {
                // Insert the leaf value into the tree
                if (node == NULL) {
                    *nodeRef = makeLeaf(value);
                    // Adjust only fp_leaf (fp will still be null)
                    tree->fp_leaf = *nodeRef;
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
                        temp_fp_path[temp_fp_path_length] = newNode;
                        temp_fp_path_length++;
                    }
                    newNode->insertNode4(this, nodeRef, existingKey[depth + newPrefixLength],
                                node, temp_fp_path, temp_fp_path_length);
                    newNode->insertNode4(this, nodeRef, key[depth + newPrefixLength],
                                makeLeaf(value), temp_fp_path, temp_fp_path_length);
                    return;
                }

                temp_fp_path[temp_fp_path_length] = node; // add the node to the array before recursion
                temp_fp_path_length++; // increase the size of the array
            
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
                                    std::copy(tree->fp_path.begin() + (temp_fp_path_length_old - 1), tree->fp_path.begin() + fp_path_length_old, fp_path_remainder.begin());
                                    tree->fp_path = temp_fp_path; // update the fp path
                                    tree->fp_path_length = temp_fp_path_length_old;
                                    // Add the remainder of fp_path to fp_path
                                    for (int i = 0; i < fp_path_length_old - temp_fp_path_length_old + 1; i++) {
                                        tree->fp_path[i + temp_fp_path_length_old] = fp_path_remainder[i];
                                        tree->fp_path_length++;
                                    }
                                    tree->fp = tree->fp_path[tree->fp_path_length - 1]; // update the fp pointer
                                }
                            }
                            newNode->insertNode4(this, nodeRef, node->prefix[mismatchPos], node,
                                    temp_fp_path, temp_fp_path_length);
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
                                    std::copy(tree->fp_path.begin() + (temp_fp_path_length_old - 1), tree->fp_path.begin() + fp_path_length_old, fp_path_remainder.begin());
                                    tree->fp_path = temp_fp_path; // update the fp path
                                    tree->fp_path_length = temp_fp_path_length_old;
                                    // Add the remainder of fp_path to fp_path
                                    for (int i = 0; i < fp_path_length_old - temp_fp_path_length_old + 1; i++) {
                                        tree->fp_path[i + temp_fp_path_length_old] = fp_path_remainder[i];
                                        tree->fp_path_length++;
                                    }
                                    tree->fp = tree->fp_path[tree->fp_path_length - 1]; // update the fp pointer
                                }
                            }
                            newNode->insertNode4(this, nodeRef, minKey[depth + mismatchPos],
                                        node, temp_fp_path, temp_fp_path_length);
                            memmove(node->prefix, minKey + depth + mismatchPos + 1,
                                    min(node->prefixLength, maxPrefixLength));
                        }
                        newNode->insertNode4(this, nodeRef, key[depth + mismatchPos],
                                    makeLeaf(value), temp_fp_path, temp_fp_path_length);
                        return;
                    }
                    depth += node->prefixLength;
                }

                // Recurse
                ArtNode** child = findChild(node, key[depth]);
                if (*child) {
                    insert(tree, *child, child, key, depth + 1, value, maxKeyLength, 
                        temp_fp_path, temp_fp_path_length);
                    return;
                }
                
                // Insert leaf into inner node
                ArtNode* newNode = makeLeaf(value);
                switch (node->type) {
                    case NodeType4:
                        static_cast<Node4*>(node)->insertNode4(this, nodeRef, key[depth],
                                    newNode, temp_fp_path, temp_fp_path_length);
                        break;
                    case NodeType16:
                        static_cast<Node16*>(node)->insertNode16(this, nodeRef, key[depth],
                                    newNode, temp_fp_path, temp_fp_path_length);
                        break;
                    case NodeType48:
                        static_cast<Node48*>(node)->insertNode48(this, nodeRef, key[depth],
                                    newNode, temp_fp_path, temp_fp_path_length);
                        break;
                    case NodeType256:
                        static_cast<Node256*>(node)->insertNode256(this, nodeRef, key[depth],
                                    newNode, temp_fp_path, temp_fp_path_length);
                        break;
                }
            }

            void printTree(ArtNode* node, int depth) {
                if (!node) return;
            
                // Indent based on depth
                for (int i = 0; i < depth; i++) {
                    printf("  ");
                }
            
                if (isLeaf(node)) {
                    printf("Leaf(%lu)\n", getLeafValue(node));
                    return;
                }
            
                switch (node->type) {
                    case NodeType4: {
                        Node4* n = static_cast<Node4*>(node);
                        printf("Node4\n");
                        for (unsigned i = 0; i < n->count; i++) {
                            printTree(n->child[i], depth + 1);
                        }
                        break;
                    }
                    case NodeType16: {
                        Node16* n = static_cast<Node16*>(node);
                        printf("Node16\n");
                        for (unsigned i = 0; i < n->count; i++) {
                            printTree(n->child[i], depth + 1);
                        }
                        break;
                    }
                    case NodeType48: {
                        Node48* n = static_cast<Node48*>(node);
                        printf("Node48\n");
                        for (unsigned i = 0; i < 256; i++) {
                            if (n->childIndex[i] != emptyMarker) {
                                printTree(n->child[n->childIndex[i]], depth + 1);
                            }
                        }
                        break;
                    }
                    case NodeType256: {
                        Node256* n = static_cast<Node256*>(node);
                        printf("Node256\n");
                        for (unsigned i = 0; i < 256; i++) {
                            if (n->child[i]) {
                                printTree(n->child[i], depth + 1);
                            }
                        }
                        break;
                    }
                }
            }
            
    };

} // namespace ART