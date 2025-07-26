#pragma once

namespace ART {
class QuART_tail : public ART {
   public:
    QuART_tail() : ART() {}

    void insert(uint8_t key[], uintptr_t value) {
        // Check if we can tail insert
        ArtNode* root = this->root;
        int leafValue = getLeafValue(this->fp_leaf);
        // Check if the root is not null and is not a leaf
        if (root != nullptr && !isLeaf(root)) {
            // For each byte in the key excluding the last byte,
            // check if it matches the corresponding byte in the leaf value
            // If any byte does not match, set can_tail_insert to false
            for (size_t i = 0; i < maxPrefixLength - 1; ++i) {
                uint8_t leafByte =
                    (leafValue >> (8 * (maxPrefixLength - 1 - i))) & 0xFF;
                if (leafByte != key[i]) {
                    // If the key differs from leafByte earlier, we tail insert
                    // from root
                    std::array<ArtNode*, maxPrefixLength> temp_fp_path = {
                        this->root};
                    size_t temp_fp_path_length = 1;
                    QuART_tail::insert_recursive_tail(
                        this, this->root, &this->root, key, 0, value,
                        maxPrefixLength, temp_fp_path, temp_fp_path_length);
                    return;
                }
            }
        } else {
            // If the root is null or is a leaf, we tail insert from root
            std::array<ArtNode*, maxPrefixLength> temp_fp_path = {this->root};
            size_t temp_fp_path_length = 1;
            QuART_tail::insert_recursive_tail(
                this, this->root, &this->root, key, 0, value, maxPrefixLength,
                temp_fp_path, temp_fp_path_length);
            return;
        }

        // We reached the last byte of the key, we can tail insert
        if (key[maxPrefixLength - 1] >= (leafValue & 0xFF)) {
            // If we can tail insert, use the fast path
            std::array<ArtNode*, maxPrefixLength> temp_fp_path = fp_path;
            size_t temp_fp_path_length = fp_path_length;
            QuART_tail::insert_recursive_tail(
                this, this->fp, this->fp_ref, key, fp_depth, value,
                maxPrefixLength, temp_fp_path, temp_fp_path_length);
            return;
        } else {
            // If we cannot tail insert, we will insert from root
            std::array<ArtNode*, maxPrefixLength> temp_fp_path = {this->root};
            size_t temp_fp_path_length = 1;
            QuART_tail::insert_recursive_tail(
                this, this->root, &this->root, key, 0, value, maxPrefixLength,
                temp_fp_path, temp_fp_path_length);
            return;
        }
    }

   private:
    void insert_recursive_tail(
        ART* tree, ArtNode* node, ArtNode** nodeRef, uint8_t key[],
        unsigned depth, uintptr_t value, unsigned maxKeyLength,
        std::array<ArtNode*, maxPrefixLength>& temp_fp_path,
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
            newNode->tailInsertNode4(
                this, nodeRef, existingKey[depth + newPrefixLength], node,
                temp_fp_path, temp_fp_path_length, depth_prev);
            newNode->tailInsertNode4(
                this, nodeRef, key[depth + newPrefixLength], makeLeaf(value),
                temp_fp_path, temp_fp_path_length, depth_prev);
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
                    // Stores the temp_fp_path and fp_path sizes before
                    // operations
                    size_t temp_fp_path_length_old = temp_fp_path_length;
                    size_t fp_path_length_old = tree->fp_path_length;
                    // In all cases, newNode should be added to fp_path
                    temp_fp_path[temp_fp_path_length_old - 1] = newNode;
                    // If the nodes that being changed is in fp_path
                    if (temp_fp_path_length_old <= fp_path_length_old &&
                        tree->fp_path[temp_fp_path_length_old - 1] == node) {
                        // If the new value is less than the current fp_leaf,
                        // restore fp_path to what it before the change with the
                        // newNode added
                        if (value < getLeafValue(tree->fp)) {
                            // A deep copy of remainder of fp_path
                            std::array<ArtNode*, maxPrefixLength>
                                fp_path_remainder;
                            std::copy(
                                tree->fp_path.begin() +
                                    (temp_fp_path_length_old - 1),
                                tree->fp_path.begin() + fp_path_length_old,
                                fp_path_remainder.begin());
                            tree->fp_path = temp_fp_path;  // update the fp path
                            tree->fp_path_length = temp_fp_path_length_old;
                            // Add the remainder of fp_path to fp_path
                            for (size_t i = 0;
                                 i < fp_path_length_old -
                                         temp_fp_path_length_old + 1;
                                 i++) {
                                tree->fp_path[i + temp_fp_path_length_old] =
                                    fp_path_remainder[i];
                                tree->fp_path_length++;
                            }
                        }
                    }
                    newNode->tailInsertNode4(
                        this, nodeRef, node->prefix[mismatchPos], node,
                        temp_fp_path, temp_fp_path_length, depth_prev);
                    node->prefixLength -= (mismatchPos + 1);
                    memmove(node->prefix, node->prefix + mismatchPos + 1,
                            min(node->prefixLength, maxPrefixLength));
                } else {
                    node->prefixLength -= (mismatchPos + 1);
                    uint8_t minKey[maxKeyLength];
                    loadKey(getLeafValue(minimum(node)), minKey);
                    // Stores the temp_fp_path and fp_path sizes before
                    // operations
                    size_t temp_fp_path_length_old = temp_fp_path_length;
                    size_t fp_path_length_old = tree->fp_path_length;
                    // In all cases, newNode should be added to fp_path
                    temp_fp_path[temp_fp_path_length_old - 1] = newNode;
                    // If the nodes that being changed is in fp_path
                    if (temp_fp_path_length_old <= fp_path_length_old &&
                        tree->fp_path[temp_fp_path_length_old - 1] == node) {
                        // If the new value is less than the current fp_leaf,
                        // restore fp_path to what it before the change with the
                        // newNode added
                        if (value < getLeafValue(tree->fp)) {
                            // A deep copy of remainder of fp_path
                            std::array<ArtNode*, maxPrefixLength>
                                fp_path_remainder;
                            std::copy(
                                tree->fp_path.begin() +
                                    (temp_fp_path_length_old - 1),
                                tree->fp_path.begin() + fp_path_length_old,
                                fp_path_remainder.begin());
                            tree->fp_path = temp_fp_path;  // update the fp path
                            tree->fp_path_length = temp_fp_path_length_old;
                            // Add the remainder of fp_path to fp_path
                            for (size_t i = 0;
                                 i < fp_path_length_old -
                                         temp_fp_path_length_old + 1;
                                 i++) {
                                tree->fp_path[i + temp_fp_path_length_old] =
                                    fp_path_remainder[i];
                                tree->fp_path_length++;
                            }
                        }
                    }
                    newNode->tailInsertNode4(
                        this, nodeRef, minKey[depth + mismatchPos], node,
                        temp_fp_path, temp_fp_path_length, depth_prev);
                    memmove(node->prefix, minKey + depth + mismatchPos + 1,
                            min(node->prefixLength, maxPrefixLength));
                }
                newNode->tailInsertNode4(
                    this, nodeRef, key[depth + mismatchPos], makeLeaf(value),
                    temp_fp_path, temp_fp_path_length, depth_prev);
                return;
            }
            depth += node->prefixLength;
        }

        // Recurse
        ArtNode** child = findChild(node, key[depth]);
        if (*child) {
            temp_fp_path[temp_fp_path_length] =
                *child;  // add the node to the array before recursion
            temp_fp_path_length++;  // increase the size of the array
            insert_recursive_tail(tree, *child, child, key, depth + 1, value,
                                  maxKeyLength, temp_fp_path,
                                  temp_fp_path_length);
            return;
        }

        // Insert leaf into inner node
        ArtNode* newNode = makeLeaf(value);
        switch (node->type) {
            case NodeType4:
                static_cast<Node4*>(node)->tailInsertNode4(
                    this, nodeRef, key[depth], newNode, temp_fp_path,
                    temp_fp_path_length, depth_prev);
                break;
            case NodeType16:
                static_cast<Node16*>(node)->tailInsertNode16(
                    this, nodeRef, key[depth], newNode, temp_fp_path,
                    temp_fp_path_length, depth_prev);
                break;
            case NodeType48:
                static_cast<Node48*>(node)->tailInsertNode48(
                    this, nodeRef, key[depth], newNode, temp_fp_path,
                    temp_fp_path_length, depth_prev);
                break;
            case NodeType256:
                static_cast<Node256*>(node)->tailInsertNode256(
                    this, nodeRef, key[depth], newNode, temp_fp_path,
                    temp_fp_path_length, depth_prev);
                break;
        }
    }
};

}  // namespace ART