#include "ART.h"
#include "ArtNode.h"

/*
 * QuART related insertNodeX methods
 */

namespace ART {
// fp insert method for Node4
void Node4::tailInsertNode4(ART* tree, ArtNode** nodeRef, uint8_t keyByte,
                            ArtNode* child,
                            std::array<ArtNode*, maxPrefixLength>& temp_fp_path,
                            size_t& temp_fp_path_length, size_t depth_prev) {
    // Insert leaf into inner node
    if (this->count < 4) {
        // Insert element
        unsigned pos;
        for (pos = 0; (pos < this->count) && (this->key[pos] < keyByte); pos++)
            ;
        // Shift keys and children to the right to make space for the new
        // key/child. This preserves the sorted order of keys in the node.
        memmove(this->key + pos + 1, this->key + pos, this->count - pos);
        memmove(this->child + pos + 1, this->child + pos,
                (this->count - pos) * sizeof(uintptr_t));
        this->key[pos] = keyByte;
        this->child[pos] = child;

        // If what's being inserted is a leaf
        if (isLeaf(child)) {
            // If the new value is greater than or equal to the current fp_leaf,
            // update the fp_leaf, fp and fp_path
            if (getLeafValue(child) >= getLeafValue(tree->fp_leaf)) {
                tree->fp_leaf = child;
                tree->fp = temp_fp_path[temp_fp_path_length - 1];
                tree->fp_path = temp_fp_path;
                tree->fp_path_length =
                    temp_fp_path_length;  // update fp_path size
                tree->fp_depth = depth_prev;
                tree->fp_ref = nodeRef;
            }
        }

        this->count++;
    } else {
        // Grow to Node16
        Node16* newNode = new Node16();
        *nodeRef = newNode;
        newNode->count = 4;
        copyPrefix(this, newNode);
        for (unsigned i = 0; i < 4; i++)
            newNode->key[i] = flipSign(this->key[i]);
        memcpy(newNode->child, this->child, this->count * sizeof(uintptr_t));

        // The sizes of temp_fp_path and fp_path before operations
        int temp_fp_path_length_old = temp_fp_path_length;
        int fp_path_length_old = tree->fp_path_length;
        // If the changing node is on the fp_path
        if (temp_fp_path_length_old <= fp_path_length_old &&
            tree->fp_path[temp_fp_path_length_old - 1] == this) {
            // Change the node to the newNode which has a greater capacity
            temp_fp_path[temp_fp_path_length_old - 1] = newNode;
            // If the new value doesn't create a new fp_leaf, restore the
            // remaining part of the fp_path
            if (getLeafValue(child) < getLeafValue(tree->fp_leaf)) {
                // create a deep copy of remainder of fp_path here
                std::array<ArtNode*, maxPrefixLength> fp_path_remainder;
                std::copy(tree->fp_path.begin() + temp_fp_path_length_old,
                          tree->fp_path.end(), fp_path_remainder.begin());
                tree->fp_path = temp_fp_path;  // update fp_path
                tree->fp_path_length =
                    temp_fp_path_length_old;  // update fp_path size
                // Add the remaining part of the fp_path
                for (int i = 0;
                     i < fp_path_length_old - temp_fp_path_length_old; i++) {
                    tree->fp_path[i + temp_fp_path_length_old] =
                        fp_path_remainder[i];
                    tree->fp_path_length++;
                }
                tree->fp = tree->fp_path[tree->fp_path_length - 1];
            }
        }

        delete this;
        return newNode->tailInsertNode16(tree, nodeRef, keyByte, child,
                                         temp_fp_path, temp_fp_path_length,
                                         depth_prev);
    }
}

// fp insert method for Node16
void Node16::tailInsertNode16(
    ART* tree, ArtNode** nodeRef, uint8_t keyByte, ArtNode* child,
    std::array<ArtNode*, maxPrefixLength>& temp_fp_path,
    size_t& temp_fp_path_length, size_t depth_prev) {
    // Insert leaf into inner node
    if (this->count < 16) {
        // Insert element

        // Flip the sign bit of the key byte for correct ordering in signed
        // comparisons
        uint8_t keyByteFlipped = flipSign(keyByte);

        // SIMD: Compare keyByteFlipped with all keys in the node in parallel
        // _mm_set1_epi8 sets all 16 bytes of an SSE register to keyByteFlipped
        // _mm_loadu_si128 loads the node's keys into an SSE register
        // _mm_cmplt_epi8 does a signed comparison of each byte
        __m128i cmp = _mm_cmplt_epi8(
            _mm_set1_epi8(keyByteFlipped),
            _mm_loadu_si128(reinterpret_cast<__m128i*>(this->key)));

        // _mm_movemask_epi8 creates a 16-bit mask from the comparison results
        // Only consider the bits for the active keys (this->count)
        uint16_t bitfield =
            _mm_movemask_epi8(cmp) & (0xFFFF >> (16 - this->count));

        // Find the position of the first set bit (i.e., where keyByteFlipped <
        // key[i])
        unsigned pos = bitfield ? ctz(bitfield) : this->count;

        // Shift keys and children to the right to make space for the new
        // key/child. This preserves the sorted order of keys in the node.
        memmove(this->key + pos + 1, this->key + pos, this->count - pos);
        memmove(this->child + pos + 1, this->child + pos,
                (this->count - pos) * sizeof(uintptr_t));
        this->key[pos] = keyByteFlipped;
        this->child[pos] = child;
        this->count++;

        // If what's being inserted is a leaf
        if (isLeaf(child)) {
            // If the new value is greater than or equal to the current fp_leaf,
            // update the fp_leaf, fp and fp_path
            if (getLeafValue(child) >= getLeafValue(tree->fp_leaf)) {
                tree->fp_leaf = child;
                tree->fp = temp_fp_path[temp_fp_path_length - 1];
                tree->fp_path = temp_fp_path;
                tree->fp_path_length = temp_fp_path_length;
                tree->fp_depth = depth_prev;
                tree->fp_ref = nodeRef;
            }
        }
    } else {
        // Grow to Node48
        Node48* newNode = new Node48();
        *nodeRef = newNode;
        memcpy(newNode->child, this->child, this->count * sizeof(uintptr_t));
        for (unsigned i = 0; i < this->count; i++)
            newNode->childIndex[flipSign(this->key[i])] = i;
        copyPrefix(this, newNode);
        newNode->count = this->count;

        // The sizes of temp_fp_path and fp_path before operations
        int temp_fp_path_length_old = temp_fp_path_length;
        int fp_path_length_old = tree->fp_path_length;
        // If the changing node is on the fp_path
        if (temp_fp_path_length_old <= fp_path_length_old &&
            tree->fp_path[temp_fp_path_length_old - 1] == this) {
            // Change the node to the newNode which has a greater capacity
            temp_fp_path[temp_fp_path_length_old - 1] = newNode;
            // If the new value doesn't create a new fp_leaf, restore the
            // remaining part of the fp_path
            if (getLeafValue(child) < getLeafValue(tree->fp_leaf)) {
                // create a deep copy of remainder of fp_path here
                std::array<ArtNode*, maxPrefixLength> fp_path_remainder;
                std::copy(tree->fp_path.begin() + temp_fp_path_length_old,
                          tree->fp_path.end(), fp_path_remainder.begin());
                tree->fp_path = temp_fp_path;  // update fp_path
                tree->fp_path_length =
                    temp_fp_path_length_old;  // update fp_path size
                // Add the remaining part of the fp_path
                for (int i = 0;
                     i < fp_path_length_old - temp_fp_path_length_old; i++) {
                    tree->fp_path[i + temp_fp_path_length_old] =
                        fp_path_remainder[i];
                    tree->fp_path_length++;
                }
                tree->fp = tree->fp_path[tree->fp_path_length -
                                         1];  // update the fp pointer
            }
        }

        delete this;
        return newNode->tailInsertNode48(tree, nodeRef, keyByte, child,
                                         temp_fp_path, temp_fp_path_length,
                                         depth_prev);
    }
}

// fp insert method for Node48
void Node48::tailInsertNode48(
    ART* tree, ArtNode** nodeRef, uint8_t keyByte, ArtNode* child,
    std::array<ArtNode*, maxPrefixLength>& temp_fp_path,
    size_t& temp_fp_path_length, size_t depth_prev) {
    // Insert leaf into inner node
    if (this->count < 48) {
        // Insert element
        unsigned pos = this->count;
        if (this->child[pos])
            for (pos = 0; this->child[pos] != NULL; pos++)
                ;
        // No memmove needed here because Node48 uses a mapping (childIndex) and
        // a dense array. The childIndex array maps key bytes to positions in
        // the child array, so insertion does not require shifting elements.
        this->child[pos] = child;
        this->childIndex[keyByte] = pos;
        this->count++;

        // If what's being inserted is a leaf
        if (isLeaf(child)) {
            // If the new value is greater than or equal to the current fp_leaf,
            // update the fp_leaf, fp and fp_path
            if (getLeafValue(child) >= getLeafValue(tree->fp_leaf)) {
                tree->fp_leaf = child;
                tree->fp = temp_fp_path[temp_fp_path_length - 1];
                tree->fp_path = temp_fp_path;
                tree->fp_path_length = temp_fp_path_length;
                tree->fp_depth = depth_prev;
                tree->fp_ref = nodeRef;
            }
        }

    } else {
        // Grow to Node256
        Node256* newNode = new Node256();
        for (unsigned i = 0; i < 256; i++)
            if (this->childIndex[i] != 48)
                newNode->child[i] = this->child[this->childIndex[i]];
        newNode->count = this->count;
        copyPrefix(this, newNode);
        *nodeRef = newNode;

        // The sizes of temp_fp_path and fp_path before operations
        int temp_fp_path_length_old = temp_fp_path_length;
        int fp_path_length_old = tree->fp_path_length;
        // If the changing node is on the fp_path
        if (temp_fp_path_length_old <= fp_path_length_old &&
            tree->fp_path[temp_fp_path_length_old - 1] == this) {
            // Change the node to the newNode which has a greater capacity
            temp_fp_path[temp_fp_path_length_old - 1] = newNode;
            // If the new value doesn't create a new fp_leaf, restore the
            // remaining part of the fp_path
            if (getLeafValue(child) < getLeafValue(tree->fp_leaf)) {
                // create a deep copy of remainder of fp_path here
                std::array<ArtNode*, maxPrefixLength> fp_path_remainder;
                std::copy(tree->fp_path.begin() + temp_fp_path_length_old,
                          tree->fp_path.end(), fp_path_remainder.begin());
                tree->fp_path = temp_fp_path;  // update fp_path
                tree->fp_path_length =
                    temp_fp_path_length_old;  // update fp_path size
                // Add the remaining part of the fp_path
                for (int i = 0;
                     i < fp_path_length_old - temp_fp_path_length_old; i++) {
                    tree->fp_path[i + temp_fp_path_length_old] =
                        fp_path_remainder[i];
                    tree->fp_path_length++;
                }
                tree->fp = tree->fp_path[tree->fp_path_length -
                                         1];  // update the fp pointer
            }
        }

        delete this;
        return newNode->tailInsertNode256(tree, nodeRef, keyByte, child,
                                          temp_fp_path, temp_fp_path_length,
                                          depth_prev);
    }
}

// fp insert method for Node256
void Node256::tailInsertNode256(
    ART* tree, ArtNode** nodeRef, uint8_t keyByte, ArtNode* child,
    std::array<ArtNode*, maxPrefixLength>& temp_fp_path,
    size_t& temp_fp_path_length, size_t depth_prev) {
    // Insert leaf into inner node
    // No memmove needed here because Node256 uses a direct mapping for all
    // possible keys. Each possible key byte directly indexes into the child
    // array, so insertion is simply an assignment.
    this->count++;
    this->child[keyByte] = child;

    // If what's being inserted is a leaf
    if (isLeaf(child)) {
        // If the new value is greater than or equal to the current fp_leaf,
        // update the fp_leaf, fp and fp_path
        if (getLeafValue(child) >= getLeafValue(tree->fp_leaf)) {
            tree->fp_leaf = child;
            tree->fp = temp_fp_path[temp_fp_path_length - 1];
            tree->fp_path = temp_fp_path;
            tree->fp_path_length = temp_fp_path_length;
            tree->fp_depth = depth_prev;
            tree->fp_ref = nodeRef;
        }
    }
}

void Node4::lilInsertNode4(ART* tree, ArtNode** nodeRef, uint8_t keyByte,
                           ArtNode* child) {
    // Insert leaf into inner node
    if (this->count < 4) {
        // Insert element
        unsigned pos;
        for (pos = 0; (pos < this->count) && (this->key[pos] < keyByte); pos++)
            ;
        memmove(this->key + pos + 1, this->key + pos, this->count - pos);
        memmove(this->child + pos + 1, this->child + pos,
                (this->count - pos) * sizeof(uintptr_t));
        this->key[pos] = keyByte;
        this->child[pos] = child;
        this->count++;
    } else {
        // Grow to Node16
        Node16* newNode = new Node16();
        *nodeRef = newNode;

        // update fast path
        tree->fp = newNode;
        tree->fp_path[tree->fp_path_length - 1] = newNode;
        tree->fp_path_ref[tree->fp_path_length - 1] = nodeRef;

        newNode->count = 4;
        copyPrefix(this, newNode);
        for (unsigned i = 0; i < 4; i++)
            newNode->key[i] = flipSign(this->key[i]);
        memcpy(newNode->child, this->child, this->count * sizeof(uintptr_t));
        delete this;
        return newNode->lilInsertNode16(tree, nodeRef, keyByte, child);
    }
}

void Node16::lilInsertNode16(ART* tree, ArtNode** nodeRef, uint8_t keyByte,
                             ArtNode* child) {
    // Insert leaf into inner node
    if (this->count < 16) {
        // Insert element
        uint8_t keyByteFlipped = flipSign(keyByte);
        __m128i cmp = _mm_cmplt_epi8(
            _mm_set1_epi8(keyByteFlipped),
            _mm_loadu_si128(reinterpret_cast<__m128i*>(this->key)));
        uint16_t bitfield =
            _mm_movemask_epi8(cmp) & (0xFFFF >> (16 - this->count));
        unsigned pos = bitfield ? ctz(bitfield) : this->count;
        memmove(this->key + pos + 1, this->key + pos, this->count - pos);
        memmove(this->child + pos + 1, this->child + pos,
                (this->count - pos) * sizeof(uintptr_t));
        this->key[pos] = keyByteFlipped;
        this->child[pos] = child;
        this->count++;
    } else {
        // Grow to Node48
        Node48* newNode = new Node48();
        *nodeRef = newNode;

        // update fast path
        tree->fp = newNode;
        tree->fp_path[tree->fp_path_length - 1] = newNode;
        tree->fp_path_ref[tree->fp_path_length - 1] = nodeRef;

        memcpy(newNode->child, this->child, this->count * sizeof(uintptr_t));
        for (unsigned i = 0; i < this->count; i++)
            newNode->childIndex[flipSign(this->key[i])] = i;
        copyPrefix(this, newNode);
        newNode->count = this->count;
        delete this;
        return newNode->lilInsertNode48(tree, nodeRef, keyByte, child);
    }
}

void Node48::lilInsertNode48(ART* tree, ArtNode** nodeRef, uint8_t keyByte,
                             ArtNode* child) {
    // Insert leaf into inner node
    if (this->count < 48) {
        // Insert element
        unsigned pos = this->count;
        if (this->child[pos])
            for (pos = 0; this->child[pos] != NULL; pos++)
                ;
        this->child[pos] = child;
        this->childIndex[keyByte] = pos;
        this->count++;
    } else {
        // Grow to Node256
        Node256* newNode = new Node256();
        for (unsigned i = 0; i < 256; i++)
            if (this->childIndex[i] != 48)
                newNode->child[i] = this->child[this->childIndex[i]];
        newNode->count = this->count;
        copyPrefix(this, newNode);
        *nodeRef = newNode;

        // update fast path
        tree->fp = newNode;
        tree->fp_path[tree->fp_path_length - 1] = newNode;
        tree->fp_path_ref[tree->fp_path_length - 1] = nodeRef;

        delete this;
        return newNode->lilInsertNode256(tree, nodeRef, keyByte, child);
    }
}

// suppress warnings about unused parameters to ensure consistency
void Node256::lilInsertNode256(ART* tree [[maybe_unused]],
                               ArtNode** nodeRef [[maybe_unused]],
                               uint8_t keyByte, ArtNode* child) {
    // Insert leaf into inner node
    this->count++;
    this->child[keyByte] = child;
}

// fp insert method for Node4 that changes fp_leaf
void Node4::stailInsertNode4ChangeFp(ART* tree, ArtNode** nodeRef,
                                     uint8_t keyByte, ArtNode* child) {
    // Insert leaf into inner node
    if (this->count < 4) {
        // Insert element
        unsigned pos;
        for (pos = 0; (pos < this->count) && (this->key[pos] < keyByte); pos++)
            ;
        memmove(this->key + pos + 1, this->key + pos, this->count - pos);
        memmove(this->child + pos + 1, this->child + pos,
                (this->count - pos) * sizeof(uintptr_t));
        this->key[pos] = keyByte;
        this->child[pos] = child;

        // Update fp parameters
        tree->fp_leaf = child;
        tree->fp = this;
        tree->fp_ref = nodeRef;

        this->count++;
    } else {
        // Grow to Node16
        Node16* newNode = new Node16();
        *nodeRef = newNode;
        newNode->count = 4;
        copyPrefix(this, newNode);
        for (unsigned i = 0; i < 4; i++)
            newNode->key[i] = flipSign(this->key[i]);
        memcpy(newNode->child, this->child, this->count * sizeof(uintptr_t));

        // Add the newNode to the fast path
        tree->fp_path[tree->fp_path_length - 1] = newNode;

        delete this;
        return newNode->stailInsertNode16ChangeFp(tree, nodeRef, keyByte,
                                                  child);
    }
}

// fp insert method for Node16 that changes fp_leaf
void Node16::stailInsertNode16ChangeFp(ART* tree, ArtNode** nodeRef,
                                       uint8_t keyByte, ArtNode* child) {
    // Insert leaf into inner node
    if (this->count < 16) {
        // Insert element
        uint8_t keyByteFlipped = flipSign(keyByte);
        __m128i cmp = _mm_cmplt_epi8(
            _mm_set1_epi8(keyByteFlipped),
            _mm_loadu_si128(reinterpret_cast<__m128i*>(this->key)));
        uint16_t bitfield =
            _mm_movemask_epi8(cmp) & (0xFFFF >> (16 - this->count));
        unsigned pos = bitfield ? ctz(bitfield) : this->count;
        memmove(this->key + pos + 1, this->key + pos, this->count - pos);
        memmove(this->child + pos + 1, this->child + pos,
                (this->count - pos) * sizeof(uintptr_t));

        this->key[pos] = keyByteFlipped;
        this->child[pos] = child;

        // Update fp parameters
        tree->fp_leaf = child;
        tree->fp = this;
        tree->fp_ref = nodeRef;

        this->count++;
    } else {
        // Grow to Node48
        Node48* newNode = new Node48();
        *nodeRef = newNode;
        memcpy(newNode->child, this->child, this->count * sizeof(uintptr_t));
        for (unsigned i = 0; i < this->count; i++)
            newNode->childIndex[flipSign(this->key[i])] = i;
        copyPrefix(this, newNode);
        newNode->count = this->count;

        // Add the newNode to the fast path
        tree->fp_path[tree->fp_path_length - 1] = newNode;

        delete this;
        return newNode->stailInsertNode48ChangeFp(tree, nodeRef, keyByte,
                                                  child);
    }
}

// fp insert method for Node48 that changes fp_leaf
void Node48::stailInsertNode48ChangeFp(ART* tree, ArtNode** nodeRef,
                                       uint8_t keyByte, ArtNode* child) {
    // Insert leaf into inner node
    if (this->count < 48) {
        // Insert element
        unsigned pos = this->count;
        if (this->child[pos])
            for (pos = 0; this->child[pos] != NULL; pos++)
                ;
        this->child[pos] = child;
        this->childIndex[keyByte] = pos;
        this->count++;

        // Update fp parameters
        tree->fp_leaf = child;
        tree->fp = this;
        tree->fp_ref = nodeRef;

    } else {
        // Grow to Node256
        Node256* newNode = new Node256();
        for (unsigned i = 0; i < 256; i++)
            if (this->childIndex[i] != 48)
                newNode->child[i] = this->child[this->childIndex[i]];
        newNode->count = this->count;
        copyPrefix(this, newNode);
        *nodeRef = newNode;

        // Add the newNode to the fast path
        tree->fp_path[tree->fp_path_length - 1] = newNode;

        delete this;
        return newNode->stailInsertNode256ChangeFp(tree, nodeRef, keyByte,
                                                   child);
    }
}

// fp insert method for Node256 that changes fp_leaf
void Node256::stailInsertNode256ChangeFp(ART* tree, ArtNode** nodeRef,
                                         uint8_t keyByte, ArtNode* child) {
    // Insert leaf into inner node
    this->count++;
    this->child[keyByte] = child;

    // Update fp parameters
    tree->fp_leaf = child;
    tree->fp = this;
    tree->fp_ref = nodeRef;
}

// fp insert method for Node4 that correctly tracks fp_ref in a
// very special case of prefix expansion
void Node4::stailInsertNode4PreserveFpPrefixExpansion(ART* tree,
                                                      ArtNode** nodeRef,
                                                      uint8_t keyByte,
                                                      ArtNode* child) {
    // Insert element
    unsigned pos;
    for (pos = 0; (pos < this->count) && (this->key[pos] < keyByte); pos++)
        ;
    memmove(this->key + pos + 1, this->key + pos, this->count - pos);
    memmove(this->child + pos + 1, this->child + pos,
            (this->count - pos) * sizeof(uintptr_t));
    this->key[pos] = keyByte;
    this->child[pos] = child;
    this->count++;

    // If the child is the fast path node, update the fast path reference
    // The child can be the fast path node ONLY in prefix expansion case
    if (child == tree->fp) {
        tree->fp_ref = &this->child[pos];
    }
}

// fp insert method for Node4 that does not change fp_leaf
void Node4::stailInsertNode4PreserveFp(ART* tree, ArtNode** nodeRef,
                                       uint8_t keyByte, ArtNode* child) {
    // Insert leaf into inner node
    if (this->count < 4) {
        // Insert element
        unsigned pos;
        for (pos = 0; (pos < this->count) && (this->key[pos] < keyByte); pos++)
            ;
        memmove(this->key + pos + 1, this->key + pos, this->count - pos);
        memmove(this->child + pos + 1, this->child + pos,
                (this->count - pos) * sizeof(uintptr_t));
        this->key[pos] = keyByte;
        this->child[pos] = child;
        this->count++;
    } else {
        // Grow to Node16
        Node16* newNode = new Node16();
        *nodeRef = newNode;
        newNode->count = 4;
        copyPrefix(this, newNode);
        for (unsigned i = 0; i < 4; i++)
            newNode->key[i] = flipSign(this->key[i]);
        memcpy(newNode->child, this->child, this->count * sizeof(uintptr_t));

        // If the changing node is the fast path node
        if (tree->fp == this) {
            // Adjust fp information
            tree->fp = newNode;
            tree->fp_path[tree->fp_path_length - 1] = newNode;
            tree->fp_ref = nodeRef;
        }
        // If the changing node hosts the cell fp_ref points to
        else if (tree->fp_path_length >= 2 && tree->fp_path[tree->fp_path_length - 2] == this) {
            tree->fp_path[tree->fp_path_length - 2] = newNode;
            // Find the cell that points to the fast path node
            // and update the fp_ref to point to the cell
            for (size_t i = 0; i < newNode->count; i++) {
                if (newNode->child[i] == tree->fp) {
                    tree->fp_ref = &newNode->child[i];
                    break;
                }
            }
        }

        delete this;

        return newNode->stailInsertNode16PreserveFp(tree, nodeRef, keyByte,
                                                    child);
    }
}

// fp insert method for Node16 that does not change fp_leaf
void Node16::stailInsertNode16PreserveFp(ART* tree, ArtNode** nodeRef,
                                         uint8_t keyByte, ArtNode* child) {
    // Insert leaf into inner node
    if (this->count < 16) {
        // Insert element
        uint8_t keyByteFlipped = flipSign(keyByte);
        __m128i cmp = _mm_cmplt_epi8(
            _mm_set1_epi8(keyByteFlipped),
            _mm_loadu_si128(reinterpret_cast<__m128i*>(this->key)));
        uint16_t bitfield =
            _mm_movemask_epi8(cmp) & (0xFFFF >> (16 - this->count));
        unsigned pos = bitfield ? ctz(bitfield) : this->count;
        memmove(this->key + pos + 1, this->key + pos, this->count - pos);
        memmove(this->child + pos + 1, this->child + pos,
                (this->count - pos) * sizeof(uintptr_t));
        this->key[pos] = keyByteFlipped;
        this->child[pos] = child;
        this->count++;
    } else {
        // Grow to Node48
        Node48* newNode = new Node48();
        *nodeRef = newNode;

        memcpy(newNode->child, this->child, this->count * sizeof(uintptr_t));
        for (unsigned i = 0; i < this->count; i++)
            newNode->childIndex[flipSign(this->key[i])] = i;
        copyPrefix(this, newNode);
        newNode->count = this->count;

        // If the changing node is the fast path node
        if (tree->fp == this) {
            tree->fp = newNode;
            tree->fp_path[tree->fp_path_length - 1] = newNode;
            tree->fp_ref = nodeRef;
        }
        // If the changing node hosts the cell fp_ref points to
        else if (tree->fp_path[tree->fp_path_length - 2] == this) {
            tree->fp_path[tree->fp_path_length - 2] = newNode;
            // Find the cell that points to the fast path node
            // and update the fp_ref to point to the cell
            for (size_t i = 0; i < newNode->count; i++) {
                if (newNode->child[i] == tree->fp) {
                    tree->fp_ref = &newNode->child[i];
                    break;
                }
            }
        }

        delete this;

        return newNode->stailInsertNode48PreserveFp(tree, nodeRef, keyByte,
                                                    child);
    }
}

void Node48::stailInsertNode48PreserveFp(ART* tree, ArtNode** nodeRef,
                                         uint8_t keyByte, ArtNode* child) {
    // Insert leaf into inner node
    if (this->count < 48) {
        // Insert element
        unsigned pos = this->count;
        if (this->child[pos])
            for (pos = 0; this->child[pos] != NULL; pos++)
                ;
        this->child[pos] = child;
        this->childIndex[keyByte] = pos;
        this->count++;
    } else {
        // Grow to Node256
        Node256* newNode = new Node256();
        for (unsigned i = 0; i < 256; i++)
            if (this->childIndex[i] != 48)
                newNode->child[i] = this->child[this->childIndex[i]];
        newNode->count = this->count;
        copyPrefix(this, newNode);
        *nodeRef = newNode;

        // If the changing node is the fast path node
        if (tree->fp == this) {
            tree->fp = newNode;
            tree->fp_path[tree->fp_path_length - 1] = newNode;
            tree->fp_ref = nodeRef;
        }
        // If the changing node hosts the cell fp_ref points to
        else if (tree->fp_path[tree->fp_path_length - 2] == this) {
            tree->fp_path[tree->fp_path_length - 2] = newNode;
            // Find the cell that points to the fast path node
            // and update the fp_ref to point to the cell
            for (size_t i = 0; i < newNode->count; i++) {
                if (newNode->child[i] == tree->fp) {
                    tree->fp_ref = &newNode->child[i];
                    break;
                }
            }
        }

        delete this;

        // There is no need for a stailInsertNode256PreserveFp method
        // because Node256 can't expand further
        return newNode->insertNode256(tree, nodeRef, keyByte, child);
    }
}

}  // namespace ART