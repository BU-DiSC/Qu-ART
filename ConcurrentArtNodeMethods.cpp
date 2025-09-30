#include "ArtNode.h"
#include "ART.h"

/*
 * Concurrent ART related insertNodeX methods
 */ 

namespace ART {
void Node4::insertNode4OLC(ART* tree, ArtNode** nodeRef, uint8_t keyByte,
        ArtNode* child, uint64_t version, ArtNode* parent, uint64_t parentVersion) {
    // Insert leaf into inner node
    if (this->count < 4) {
        // Insert element

        upgradeToWriteLockOrRestart(node, version);
        readUnlockOrRestart(parent, parentVersion, node);


        unsigned pos;
        for (pos = 0; (pos < this->count) && (this->key[pos] < keyByte); pos++);
        memmove(this->key + pos + 1, this->key + pos, this->count - pos);
        memmove(this->child + pos + 1, this->child + pos,
                (this->count - pos) * sizeof(uintptr_t));
        this->key[pos] = keyByte;
        this->child[pos] = child;
        this->count++;

        writeUnlock(node);

    } else {
        // Grow to Node16

        upgradeToWriteLockOrRestart(parent, parentVersion);
        upgradeToWriteLockOrRestart(node, version, parent);

        Node16* newNode = new Node16();
        *nodeRef = newNode;
        newNode->count = 4;
        copyPrefix(this, newNode);
        for (unsigned i = 0; i < 4; i++)
            newNode->key[i] = flipSign(this->key[i]);
        memcpy(newNode->child, this->child, this->count * sizeof(uintptr_t));
        delete this;

        writeUnlockObselete(node);
        writeUnlock(parent);

        return newNode->insertNode16OLC(tree, nodeRef, keyByte, child, 
            version, parent, parentVersion);
    }
}

    
void Node16::insertNode16(ART* tree, ArtNode** nodeRef, uint8_t keyByte,
            ArtNode* child) {
    // Insert leaf into inner node
    if (this->count < 16) {
        // Insert element

        upgradeToWriteLockOrRestart(node, version);
        readUnlockOrRestart(parent, parentVersion, node);

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

        writeUnlock(node);

    } else 
        // Grow to Node48

        upgradeToWriteLockOrRestart(parent, parentVersion);
        upgradeToWriteLockOrRestart(node, version, parent);

        Node48* newNode = new Node48();
        *nodeRef = newNode;
        memcpy(newNode->child, this->child, this->count * sizeof(uintptr_t));
        for (unsigned i = 0; i < this->count; i++)
            newNode->childIndex[flipSign(this->key[i])] = i;
        copyPrefix(this, newNode);
        newNode->count = this->count;
        delete this;

        writeUnlockObselete(node);
        writeUnlock(parent);

        return newNode->insertNode48OLC(tree, nodeRef, keyByte, child, 
            version, parent, parentVersion);
}

void Node48::insertNode48OLC(ART* tree, ArtNode** nodeRef, uint8_t keyByte, ArtNode* child,
        uint64_t version, ArtNode* parent, uint64_t parentVersion) {
    // Insert leaf into inner node
    if (this->count < 48) {
        // Insert element

        upgradeToWriteLockOrRestart(node, version);
        readUnlockOrRestart(parent, parentVersion, node);

        unsigned pos = this->count;
        if (this->child[pos])
            for (pos = 0; this->child[pos] != NULL; pos++);
                this->child[pos] = child;
                this->childIndex[keyByte] = pos;
                    this->count++;

        writeUnlock(node);

    } else {
        // Grow to Node256

        upgradeToWriteLockOrRestart(parent, parentVersion);
        upgradeToWriteLockOrRestart(node, version, parent);

        Node256* newNode = new Node256();
        for (unsigned i = 0; i < 256; i++)
        if (this->childIndex[i] != 48)
            newNode->child[i] = this->child[this->childIndex[i]];
        newNode->count = this->count;
        copyPrefix(this, newNode);
        *nodeRef = newNode;
        delete this;

        writeUnlockObselete(node);
        writeUnlock(parent);

        return newNode->insertNode256OLC(tree, nodeRef, keyByte, child, 
            version, parent, parentVersion);
    }
}

void Node256::insertNode256OLC(ART* tree, ArtNode** nodeRef, uint8_t keyByte, ArtNode* child,
        uint64_t version, ArtNode* parent, uint64_t parentVersion) {

    upgradeToWriteLockOrRestart(node, version);
    readUnlockOrRestart(parent, parentVersion, node);

    // Insert leaf into inner node
    this->count++;
    this->child[keyByte] = child;

    writeUnlock(node);
}  

}