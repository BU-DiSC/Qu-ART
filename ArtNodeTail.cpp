#include "ArtNode.h"
#include "ART.h"

// Insert methods for structs Node4, Node16, Node48, and Node256
namespace ART {
    // fp insert method for Node4
    void Node4::insertNode4(ART* tree, ArtNode** nodeRef, uint8_t keyByte,
        ArtNode* child, std::array<ArtNode*, maxPrefixLength> temp_fp_path,
        size_t temp_fp_path_length) {
        // Insert leaf into inner node
        if (this->count < 4) {
            // Insert element
            unsigned pos;
            for (pos = 0; (pos < this->count) && (this->key[pos] < keyByte); pos++);
            memmove(this->key + pos + 1, this->key + pos, this->count - pos);
            memmove(this->child + pos + 1, this->child + pos,
                    (this->count - pos) * sizeof(uintptr_t));
            this->key[pos] = keyByte;
            this->child[pos] = child;
            
            // If what's being inserted is a leaf
            if (isLeaf(this->child[pos])) {
                // If the new value is greater than or equal to the current fp_leaf, 
                // update the fp_leaf, fp and fp_path
                if (getLeafValue(child) >= getLeafValue(tree->fp_leaf)) {
                    tree->fp_leaf = this->child[pos];
                    tree->fp = temp_fp_path[temp_fp_path_length - 1]; 
                    tree->fp_path = temp_fp_path;
                    tree->fp_path_length = temp_fp_path_length; // update fp_path size
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
            if (temp_fp_path_length_old <= fp_path_length_old && tree->fp_path[temp_fp_path_length_old-1] == this) {
                // Change the node to the newNode which has a greater capacity
                temp_fp_path[temp_fp_path_length_old - 1] = newNode;
                // If the new value doesn't create a new fp_leaf, restore the remaining part of the fp_path
                if (getLeafValue(child) < getLeafValue(tree->fp_leaf)) {
                    // create a deep copy of remainder of fp_path here
                    std::array<ArtNode*, maxPrefixLength> fp_path_remainder;
                    std::copy(tree->fp_path.begin() + temp_fp_path_length_old, tree->fp_path.end(), fp_path_remainder.begin());
                    tree->fp_path = temp_fp_path; // update fp_path
                    tree->fp_path_length = temp_fp_path_length_old; // update fp_path size
                    // Add the remaining part of the fp_path
                    for (int i = 0; i < fp_path_length_old - temp_fp_path_length_old; i++) {
                        tree->fp_path[i + temp_fp_path_length_old] = fp_path_remainder[i];
                        tree->fp_path_length++;
                    }
                    tree->fp = tree->fp_path[tree->fp_path_length - 1]; // update the fp pointer
                }
            }

            delete this;
            return newNode->insertNode16(tree, nodeRef, keyByte, child, temp_fp_path, temp_fp_path_length);
        }
    }

    // fp insert method for Node16
    void Node16::insertNode16(ART* tree, ArtNode** nodeRef, uint8_t keyByte,
        ArtNode* child, std::array<ArtNode*, maxPrefixLength> temp_fp_path,
        size_t temp_fp_path_length) {
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

            // If what's being inserted is a leaf
            if (isLeaf(this->child[pos])) {
                // If the new value is greater than or equal to the current fp_leaf, 
                // update the fp_leaf, fp and fp_path
                if (getLeafValue(child) >= getLeafValue(tree->fp_leaf)) {
                    tree->fp_leaf = this->child[pos];
                    tree->fp = temp_fp_path[temp_fp_path_length - 1]; 
                    tree->fp_path = temp_fp_path;
                    tree->fp_path_length = temp_fp_path_length; // update fp_path size
                }
            }

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

            // The sizes of temp_fp_path and fp_path before operations
            int temp_fp_path_length_old = temp_fp_path_length;
            int fp_path_length_old = tree->fp_path_length;
            // If the changing node is on the fp_path
            if (temp_fp_path_length_old <= fp_path_length_old && tree->fp_path[temp_fp_path_length_old-1] == this) {
                // Change the node to the newNode which has a greater capacity
                temp_fp_path[temp_fp_path_length_old - 1] = newNode;
                // If the new value doesn't create a new fp_leaf, restore the remaining part of the fp_path
                if (getLeafValue(child) < getLeafValue(tree->fp_leaf)) {
                    // create a deep copy of remainder of fp_path here
                    std::array<ArtNode*, maxPrefixLength> fp_path_remainder;
                    std::copy(tree->fp_path.begin() + temp_fp_path_length_old, tree->fp_path.end(), fp_path_remainder.begin());
                    tree->fp_path = temp_fp_path; // update fp_path
                    tree->fp_path_length = temp_fp_path_length_old; // update fp_path size
                    // Add the remaining part of the fp_path
                    for (int i = 0; i < fp_path_length_old - temp_fp_path_length_old; i++) {
                        tree->fp_path[i + temp_fp_path_length_old] = fp_path_remainder[i];
                        tree->fp_path_length++;
                    }
                    tree->fp = tree->fp_path[tree->fp_path_length - 1]; // update the fp pointer
                }
            }

            delete this;
            return newNode->insertNode48(tree, nodeRef, keyByte, child, temp_fp_path, temp_fp_path_length);
        }
    }

    // fp insert method for Node48
    void Node48::insertNode48(ART* tree, ArtNode** nodeRef, uint8_t keyByte, ArtNode* child,
        std::array<ArtNode*, maxPrefixLength> temp_fp_path, size_t temp_fp_path_length) {
       // Insert leaf into inner node
       if (this->count < 48) {
           // Insert element
           unsigned pos = this->count;
           if (this->child[pos])
               for (pos = 0; this->child[pos] != NULL; pos++);
                   this->child[pos] = child;
           this->childIndex[keyByte] = pos;

           // If what's being inserted is a leaf
           if (isLeaf(this->child[pos])) {
               // If the new value is greater than or equal to the current fp_leaf, 
               // update the fp_leaf, fp and fp_path
               if (getLeafValue(child) >= getLeafValue(tree->fp_leaf)) {
                   tree->fp_leaf = this->child[pos];
                   tree->fp = temp_fp_path[temp_fp_path_length - 1]; 
                   tree->fp_path = temp_fp_path;
                   tree->fp_path_length = temp_fp_path_length; // update fp_path size
               }
           }

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

           // The sizes of temp_fp_path and fp_path before operations
           int temp_fp_path_length_old = temp_fp_path_length;
           int fp_path_length_old = tree->fp_path_length;
           // If the changing node is on the fp_path
           if (temp_fp_path_length_old <= fp_path_length_old && tree->fp_path[temp_fp_path_length_old-1] == this) {
               // Change the node to the newNode which has a greater capacity
               temp_fp_path[temp_fp_path_length_old - 1] = newNode;
               // If the new value doesn't create a new fp_leaf, restore the remaining part of the fp_path
               if (getLeafValue(child) < getLeafValue(tree->fp_leaf)) {
                   // create a deep copy of remainder of fp_path here
                   std::array<ArtNode*, maxPrefixLength> fp_path_remainder;
                   std::copy(tree->fp_path.begin() + temp_fp_path_length_old, tree->fp_path.end(), fp_path_remainder.begin());
                   tree->fp_path = temp_fp_path; // update fp_path
                   tree->fp_path_length = temp_fp_path_length_old; // update fp_path size
                   // Add the remaining part of the fp_path
                   for (int i = 0; i < fp_path_length_old - temp_fp_path_length_old; i++) {
                       tree->fp_path[i + temp_fp_path_length_old] = fp_path_remainder[i];
                       tree->fp_path_length++;
                   }
                   tree->fp = tree->fp_path[tree->fp_path_length - 1]; // update the fp pointer
               }
           }

           delete this;
           return newNode->insertNode256(tree, nodeRef, keyByte, child, temp_fp_path, temp_fp_path_length);
       }
    }

    // fp insert method for Node256
    void Node256::insertNode256(ART* tree, ArtNode** nodeRef, uint8_t keyByte,
        ArtNode* child, std::array<ArtNode*, maxPrefixLength> temp_fp_path, 
        size_t temp_fp_path_length) {
        // Insert leaf into inner node
        this->count++;
        this->child[keyByte] = child;

        // If what's being inserted is a leaf
        if (isLeaf(this->child[keyByte])) {
            // If the new value is greater than or equal to the current fp_leaf, 
            // update the fp_leaf, fp and fp_path
            if (getLeafValue(child) >= getLeafValue(tree->fp_leaf)) {
                tree->fp_leaf = this->child[keyByte];
                tree->fp = temp_fp_path[temp_fp_path_length - 1]; 
                tree->fp_path = temp_fp_path;
                tree->fp_path_length = temp_fp_path_length; // update fp_path size
            }
        }

    }  


}