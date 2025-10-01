#pragma once

#include "ART.h"
#include "ArtNode.h"
#include "OLC.h"
#include "ConcurrentArtNodeMethods.cpp"

namespace ART {

class ConcurrentART : public ART {
public:
    ConcurrentART() : ART() {}

    void insert(uint8_t key[], uintptr_t value) {
        while (true) {
            try {
                insert(root, &root, key, 0, value, maxPrefixLength, nullptr, 0);
                break;
            } catch (const RestartException&) {
                // Restart on OLC conflict
                continue;
            }
        }
    }

private:
    void insert(ArtNode* node, ArtNode** nodeRef, uint8_t key[], unsigned depth,
                        uintptr_t value, unsigned maxKeyLength, ArtNode* parent, uint64_t parentVersion) {

        // Insert the leaf value into the tree
        if (node == NULL) {
            *nodeRef = makeLeaf(value);
            return;
        }

        if (isLeaf(node)) {

            // TODO: think about the correct location for it
            if (parent) {
                readUnlockOrRestart(parent, parentVersion);
                upgradeToWriteLockOrRestart(parent, parentVersion);
            }

            // Replace leaf with Node4 and store both leaves in it
            uint8_t existingKey[maxKeyLength];
            loadKey(getLeafValue(node), existingKey);
            unsigned newPrefixLength = 0;
            while (existingKey[depth + newPrefixLength] == key[depth + newPrefixLength])
                newPrefixLength++;

            Node4* newNode = new Node4();
            newNode->prefixLength = newPrefixLength;
            memcpy(newNode->prefix, key + depth, min(newPrefixLength, maxPrefixLength));
            *nodeRef = newNode;
            
            newNode->insertNode4OLC(nullptr, nodeRef, existingKey[depth + newPrefixLength], node, 
                newNode->version, parent, parent ? parentVersion : -1);
            newNode->insertNode4OLC(nullptr, nodeRef, key[depth + newPrefixLength], makeLeaf(value), 
                newNode->version, parent, parent ? parentVersion : -1);

            if (parent) writeUnlock(parent);

            return;
        }

        uint64_t version = readLockOrRestart(node);

        // Check prefix
        if (node->prefixLength) {
            unsigned mismatchPos = prefixMismatch(node, key, depth, maxKeyLength);
            if (mismatchPos != node->prefixLength) {

                upgradeToWriteLockOrRestart(parent, parentVersion);
                upgradeToWriteLockOrRestart(node, version, parent);

                // Prefix differs, create new node
                Node4* newNode = new Node4();
                *nodeRef = newNode;
                newNode->prefixLength = mismatchPos;
                memcpy(newNode->prefix, node->prefix, min(mismatchPos, maxPrefixLength));

                // Break up prefix
                if (node->prefixLength < maxPrefixLength) {
                    newNode->insertNode4OLC(nullptr, nodeRef, node->prefix[mismatchPos], node, 
                        version, parent, parentVersion);
                    node->prefixLength -= (mismatchPos + 1);
                    memmove(node->prefix, node->prefix + mismatchPos + 1,
                           min(node->prefixLength, maxPrefixLength));
                } else {
                    node->prefixLength -= (mismatchPos + 1);
                    uint8_t minKey[maxKeyLength];
                    loadKey(getLeafValue(minimum(node)), minKey);
                    newNode->insertNode4OLC(nullptr, nodeRef, minKey[depth + mismatchPos], node, 
                        version, parent, parentVersion);
                    memmove(node->prefix, minKey + depth + mismatchPos + 1,
                           min(node->prefixLength, maxPrefixLength));
                }
                newNode->insertNode4OLC(nullptr, nodeRef, key[depth + mismatchPos], makeLeaf(value), 
                    version, parent, parentVersion);
                
                if (parent) writeUnlock(parent);

                writeUnlock(node);
                writeUnlock(parent);
                
                return;
            }
            depth += node->prefixLength;
        }

        // Find child node
        ArtNode** child = findChild(node, key[depth]);

        checkOrRestart(node, version);
        
        if (*child) {
            // TODO: think about the correct location for it
            if (parent != nullptr) {
                readUnlockOrRestart(parent, parentVersion);
            }
            insert(*child, child, key, depth + 1, value, maxKeyLength, node, version);
            return;
        }
        // Insert leaf into inner node
        ArtNode* newNode = makeLeaf(value);
        
        switch (node->type) {
            case NodeType4:
                static_cast<Node4*>(node)->insertNode4OLC(nullptr, nodeRef, key[depth], newNode,
                    version, parent, parentVersion);
                break;
            case NodeType16:
                static_cast<Node16*>(node)->insertNode16OLC(nullptr, nodeRef, key[depth], newNode,
                    version, parent, parentVersion);
                break;
            case NodeType48:
                static_cast<Node48*>(node)->insertNode48OLC(nullptr, nodeRef, key[depth], newNode,
                    version, parent, parentVersion);
                break;
            case NodeType256:
                static_cast<Node256*>(node)->insertNode256OLC(nullptr, nodeRef, key[depth], newNode,
                    version, parent, parentVersion);
                break;
        }

    }

};

} // namespace ART