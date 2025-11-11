#pragma once

#include "../ART.h"
#include "../ArtNode.h"
#include "VersionControl.h"
#include "VersionControl.cpp"
#include <mutex>
#include <shared_mutex>


namespace ART {

class ConcurrentART : public ART {
   public:
    ConcurrentART() : ART() {}

    void insert(uint8_t key[], uintptr_t value) {
        ART::insert(key, value);
    }

    void insertCC(uint8_t key[], uintptr_t value) {
        while (true) {  // Retry loop
            try {
                insert(this, root, &root, key, 0, value, maxPrefixLength, nullptr, 0);
                return;  // Success - exit retry loop
            } catch (const RestartException&) {
                // Conflict detected - loop will retry from beginning
                continue;
            }
        }
    }

   private:
    
    bool concurrent_mode;

    void insert(ConcurrentART* tree, ArtNode* node, ArtNode** nodeRef, uint8_t key[], int depth, uintptr_t value, int maxKeyLength, ArtNode* parent, int parentVersion) {
        // Insert the leaf value into the tree

        uint64_t version = readLockOrRestart(node);

        // Handle prefix of inner node
        if (node->prefixLength) {
            unsigned mismatchPos =
                prefixMismatch(node, key, depth, maxKeyLength);
            if (mismatchPos != node->prefixLength) {
                // Prefix differs, create new node

                if (parent) {
                    upgradeToWriteLockOrRestart(parent, parentVersion);
                }
                upgradeToWriteLockOrRestart(node, version, parent);

                Node4* newNode = new Node4();
                newNode->prefixLength = mismatchPos;
                memcpy(newNode->prefix, node->prefix,
                       min(mismatchPos, maxPrefixLength));
                // Break up prefix
                if (node->prefixLength < maxPrefixLength) {
                    newNode->insertNode4(this, nodeRef,
                                         node->prefix[mismatchPos], node);
                    node->prefixLength -= (mismatchPos + 1);
                    memmove(node->prefix, node->prefix + mismatchPos + 1,
                            min(node->prefixLength, maxPrefixLength));
                } else {
                    node->prefixLength -= (mismatchPos + 1);
                    uint8_t minKey[maxKeyLength];
                    loadKey(getLeafValue(minimum(node)), minKey);
                    newNode->insertNode4(this, nodeRef,
                                         minKey[depth + mismatchPos], node);
                    memmove(node->prefix, minKey + depth + mismatchPos + 1,
                            min(node->prefixLength, maxPrefixLength));
                }
                newNode->insertNode4(this, nodeRef, key[depth + mismatchPos],
                                     makeLeaf(value));
                *nodeRef = newNode;
                writeUnlock(node);
                if (parent) { writeUnlock(parent); }

                return;
            }
            depth += node->prefixLength;
        }

        // Recurse
        ArtNode** child = findChild(node, key[depth]);

        checkOrRestart(node, version);

        if (!*child) {
            // Insert leaf into inner node
            ArtNode* newNode = makeLeaf(value);
            uint8_t keyByte = key[depth];

            switch (node->type) {
                case NodeType4: {
                    // Cast node to Node4 to access its members
                    Node4* node4 = static_cast<Node4*>(node);
                    // Insert leaf into inner node
                    if (node4->count < 4) {

                        upgradeToWriteLockOrRestart(node, version);
                        if (parent) { readUnlockOrRestart(parent, parentVersion, node); }

                        // Insert element
                        unsigned pos;
                        for (pos = 0; (pos < node4->count) && (node4->key[pos] < keyByte); pos++)
                            ;
                        // Shift keys and children to the right to make space for the new
                        // key/child. This preserves the sorted order of keys in the node.
                        memmove(node4->key + pos + 1, node4->key + pos, node4->count - pos);
                        memmove(node4->child + pos + 1, node4->child + pos,
                                (node4->count - pos) * sizeof(uintptr_t));
                        node4->key[pos] = keyByte;
                        node4->child[pos] = newNode;  // Use newNode, not child
                        node4->count++;

                        writeUnlock(node);

                    } else {

                        if (parent) {
                            upgradeToWriteLockOrRestart(parent, parentVersion);
                        }
                        upgradeToWriteLockOrRestart(node, version, parent);

                        // Grow to Node16
                        Node16* newNode16 = new Node16();
                        newNode16->count = 4;
                        copyPrefix(node4, newNode16);
                        for (unsigned i = 0; i < 4; i++)
                            newNode16->key[i] = flipSign(node4->key[i]);
                        memcpy(newNode16->child, node4->child, node4->count * sizeof(uintptr_t));

                        // Shouldn't be deleted, other threads might still be reading it
                        // delete node;

                        // INSERT NODE 16 PART
                        
                        // Flip the sign bit of the key byte for correct ordering in signed
                        // comparisons
                        uint8_t keyByteFlipped = flipSign(keyByte);

                        // SIMD: Compare keyByteFlipped with all keys in the node in parallel
                        // _mm_set1_epi8 sets all 16 bytes of an SSE register to keyByteFlipped
                        // _mm_loadu_si128 loads the node's keys into an SSE register
                        // _mm_cmplt_epi8 does a signed comparison of each byte
                        __m128i cmp = _mm_cmplt_epi8(
                            _mm_set1_epi8(keyByteFlipped),
                            _mm_loadu_si128(reinterpret_cast<__m128i*>(newNode16->key)));

                        // _mm_movemask_epi8 creates a 16-bit mask from the comparison results
                        // Only consider the bits for the active keys (node16->count)
                        uint16_t bitfield =
                            _mm_movemask_epi8(cmp) & (0xFFFF >> (16 - newNode16->count));

                        // Find the position of the first set bit (i.e., where keyByteFlipped <
                        // key[i])
                        unsigned pos = bitfield ? ctz(bitfield) : newNode16->count;

                        // Shift keys and children to the right to make space for the new
                        // key/child. This preserves the sorted order of keys in the node.
                        memmove(newNode16->key + pos + 1, newNode16->key + pos, newNode16->count - pos);
                        memmove(newNode16->child + pos + 1, newNode16->child + pos,
                                (newNode16->count - pos) * sizeof(uintptr_t));
                        newNode16->key[pos] = keyByteFlipped;
                        newNode16->child[pos] = newNode;
                        newNode16->count++;
                        *nodeRef = newNode16;

                        writeUnlockObsolete(node);
                        if (parent) { writeUnlock(parent); }
                        
                    }
                    break;
                }
                case NodeType16: {
                    // Cast node to Node16 to access its members
                    Node16* node16 = static_cast<Node16*>(node);

                    // Insert leaf into inner node
                    if (node16->count < 16) {
                        // Insert element
                        
                        upgradeToWriteLockOrRestart(node, version);
                        if (parent) { readUnlockOrRestart(parent, parentVersion, node); }

                        // Flip the sign bit of the key byte for correct ordering in signed
                        // comparisons
                        uint8_t keyByteFlipped = flipSign(keyByte);

                        // SIMD: Compare keyByteFlipped with all keys in the node in parallel
                        // _mm_set1_epi8 sets all 16 bytes of an SSE register to keyByteFlipped
                        // _mm_loadu_si128 loads the node's keys into an SSE register
                        // _mm_cmplt_epi8 does a signed comparison of each byte
                        __m128i cmp = _mm_cmplt_epi8(
                            _mm_set1_epi8(keyByteFlipped),
                            _mm_loadu_si128(reinterpret_cast<__m128i*>(node16->key)));

                        // _mm_movemask_epi8 creates a 16-bit mask from the comparison results
                        // Only consider the bits for the active keys (node16->count)
                        uint16_t bitfield =
                            _mm_movemask_epi8(cmp) & (0xFFFF >> (16 - node16->count));

                        // Find the position of the first set bit (i.e., where keyByteFlipped <
                        // key[i])
                        unsigned pos = bitfield ? ctz(bitfield) : node16->count;

                        // Shift keys and children to the right to make space for the new
                        // key/child. This preserves the sorted order of keys in the node.
                        memmove(node16->key + pos + 1, node16->key + pos, node16->count - pos);
                        memmove(node16->child + pos + 1, node16->child + pos,
                                (node16->count - pos) * sizeof(uintptr_t));
                        node16->key[pos] = keyByteFlipped;
                        node16->child[pos] = newNode;
                        node16->count++;

                        writeUnlock(node);

                    } else {

                        if (parent) {
                            upgradeToWriteLockOrRestart(parent, parentVersion);
                        }
                        upgradeToWriteLockOrRestart(node, version, parent);

                        // Grow to Node48
                        Node48* newNode48 = new Node48();
                        
                        memcpy(newNode48->child, node16->child, node16->count * sizeof(uintptr_t));
                        for (unsigned i = 0; i < node16->count; i++)
                            newNode48->childIndex[flipSign(node16->key[i])] = i;
                        copyPrefix(node16, newNode48);
                        newNode48->count = node16->count;

                        // Shouldn't be deleted, other threads might still be reading it
                        // delete node;
                        
                        // INSERT NODE 48 PART

                        // Insert element
                        unsigned pos = newNode48->count;
                        if (newNode48->child[pos])
                            for (pos = 0; newNode48->child[pos] != NULL; pos++)
                                ;
                        // No memmove needed here because Node48 uses a mapping (childIndex) and
                        // a dense array.
                        newNode48->child[pos] = newNode;
                        newNode48->childIndex[keyByte] = pos;
                        newNode48->count++;
                        *nodeRef = newNode48;

                        writeUnlockObsolete(node);
                        if (parent) { writeUnlock(parent); }
                    }
                    break;
                }
                case NodeType48: {
                    // Cast node to Node48 to access its members
                    Node48* node48 = static_cast<Node48*>(node);

                    // Insert leaf into inner node
                    if (node48->count < 48) {

                        upgradeToWriteLockOrRestart(node, version);
                        if (parent) { readUnlockOrRestart(parent, parentVersion, node); }

                        // Insert element
                        unsigned pos = node48->count;
                        if (node48->child[pos])
                            for (pos = 0; node48->child[pos] != NULL; pos++)
                                ;
                        // No memmove needed here because Node48 uses a mapping (childIndex) and
                        // a dense array.
                        node48->child[pos] = newNode;
                        node48->childIndex[keyByte] = pos;
                        node48->count++;

                        writeUnlock(node);

                    } else {
                        // Grow to Node256

                        if (parent) {
                            upgradeToWriteLockOrRestart(parent, parentVersion);
                        }
                        upgradeToWriteLockOrRestart(node, version, parent);

                        Node256* newNode256 = new Node256();
                        for (unsigned i = 0; i < 256; i++)
                            if (node48->childIndex[i] != 48)
                                newNode256->child[i] = node48->child[node48->childIndex[i]];
                        newNode256->count = node48->count;
                        copyPrefix(node48, newNode256);

                        // tree->printTree();

                        // Shouldn't be deleted, other threads might still be reading it
                        // delete node;

                        // INSERT NODE 256 PART

                        // Insert leaf into inner node
                        // No memmove needed here because Node256 uses a direct mapping for all
                        // possible keys.
                        newNode256->count++;
                        newNode256->child[keyByte] = newNode;
                        *nodeRef = newNode256;

                        writeUnlockObsolete(node);
                        if (parent) { writeUnlock(parent); }
                    }
                    break;
                }
                case NodeType256: {
                    // Cast node to Node256 to access its members
                    Node256* node256 = static_cast<Node256*>(node);

                    upgradeToWriteLockOrRestart(node, version);
                    if (parent) { readUnlockOrRestart(parent, parentVersion, node); }

                    // Insert leaf into inner node
                    // No memmove needed here because Node256 uses a direct mapping for all
                    // possible keys.
                    node256->count++;
                    node256->child[keyByte] = newNode;

                    writeUnlock(node);

                    break;
                }
            }
            return;
        }

        if (parent) {
            readUnlockOrRestart(parent, parentVersion);
        }

        if (isLeaf(*child)) {

            upgradeToWriteLockOrRestart(node, version);

            depth++;
            // Replace leaf with Node4 and store both leaves in it
            uint8_t existingKey[maxKeyLength];
            loadKey(getLeafValue(*child), existingKey);
            unsigned newPrefixLength = 0;
            while (existingKey[depth + newPrefixLength] ==
                   key[depth + newPrefixLength])
                newPrefixLength++;

            Node4* newNode = new Node4();
            newNode->prefixLength = newPrefixLength;
            memcpy(newNode->prefix, key + depth,
                   min(newPrefixLength, maxPrefixLength));
            
            ArtNode* oldLeaf = *child;

            newNode->insertNode4(this, child,
                                 existingKey[depth + newPrefixLength], oldLeaf);
            newNode->insertNode4(this, child, key[depth + newPrefixLength],
                                 makeLeaf(value));
            
            *child = newNode;
            writeUnlock(node);

            return;
        }

        insert(tree, *child, child, key, depth + 1, value, maxKeyLength, node, version);
        return; 
   }
};

}  // namespace ART