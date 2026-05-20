#pragma once

#include "../ART.h"
#include "../ArtNode.h"

namespace ART {

// Abstract base class for all QuART insertion variants.
// Inherits ART's node structure, fp state, and lookup semantics.
// Provides the shared recursive insertion infrastructure used by
// all variants (tail, lil, stail), and requires each variant to
// implement its own insert() routing policy.
class QuART : public ART {
   public:
    QuART() : ART() {}

    virtual void insert(uint8_t key[], uintptr_t value) = 0;

   protected:
    void insert_recursive_preserve_fp(ArtNode* node, ArtNode** nodeRef,
                                      uint8_t key[], unsigned depth,
                                      uintptr_t value, unsigned maxKeyLength,
                                      ArtNode* prevNode = nullptr) {
        // If leaf expansion is needed
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

            // If the changing node was the fp
            if (this->fp_leaf == node) {
                // If fp is not null (used only to avoid second insert)
                if (!isLeaf(this->fp)) {
                    this->fp_depth += fp->prefixLength;
                    this->fp_depth++;
                }
                // Adjust fp parameters
                this->fp = newNode;
                this->fp_ref = nodeRef;
                this->fp_prev = prevNode;
            }

            newNode->insertNode4(this, nodeRef,
                                 existingKey[depth + newPrefixLength], node);
            newNode->insertNode4(this, nodeRef, key[depth + newPrefixLength],
                                 makeLeaf(value));
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
                    // If node is fp, a new node is being spliced above it
                    if (node == this->fp) {
                        this->fp_prev = newNode;
                        this->fp_depth += newNode->prefixLength;
                        this->fp_depth++;
                    }
                    newNode->insertNode4PreserveFpPrefixExpansion(
                        this, nodeRef, node->prefix[mismatchPos], node);
                    node->prefixLength -= (mismatchPos + 1);
                    memmove(node->prefix, node->prefix + mismatchPos + 1,
                            min(node->prefixLength, maxPrefixLength));
                } else {
                    node->prefixLength -= (mismatchPos + 1);
                    uint8_t minKey[maxKeyLength];
                    loadKey(getLeafValue(minimum(node)), minKey);
                    // If node is fp, a new node is being spliced above it
                    if (node == this->fp) {
                        this->fp_prev = newNode;
                        this->fp_depth += newNode->prefixLength;
                        this->fp_depth++;
                    }
                    newNode->insertNode4PreserveFpPrefixExpansion(
                        this, nodeRef, minKey[depth + mismatchPos], node);
                    memmove(node->prefix, minKey + depth + mismatchPos + 1,
                            min(node->prefixLength, maxPrefixLength));
                }
                newNode->insertNode4(this, nodeRef, key[depth + mismatchPos],
                                     makeLeaf(value));
                return;
            }
            depth += node->prefixLength;
        }

        // Recurse
        ArtNode** child = findChild(node, key[depth]);
        if (*child) {
            insert_recursive_preserve_fp(*child, child, key, depth + 1, value,
                                         maxKeyLength, node);
            return;
        }

        // Insert leaf into inner node
        ArtNode* newNode = makeLeaf(value);
        switch (node->type) {
            case NodeType4:
                static_cast<Node4*>(node)->insertNode4PreserveFp(
                    this, nodeRef, key[depth], newNode);
                break;
            case NodeType16:
                static_cast<Node16*>(node)->insertNode16PreserveFp(
                    this, nodeRef, key[depth], newNode);
                break;
            case NodeType48:
                static_cast<Node48*>(node)->insertNode48PreserveFp(
                    this, nodeRef, key[depth], newNode);
                break;
            case NodeType256:
                static_cast<Node256*>(node)->insertNode256(this, nodeRef,
                                                           key[depth], newNode);
                break;
        }
    }

    void insert_recursive_change_fp(ArtNode* node, ArtNode** nodeRef,
                                    uint8_t key[], unsigned depth,
                                    uintptr_t value, unsigned maxKeyLength,
                                    ArtNode* prevNode = nullptr) {
        // Insert the leaf
        if (node == NULL) {
            *nodeRef = makeLeaf(value);
            // Adjust fp parameters
            this->fp_leaf = *nodeRef;
            this->fp = *nodeRef;
            this->fp_ref = nodeRef;
            this->fp_depth = 0;
            this->fp_prev = prevNode;
            return;
        }

        // If leaf expansion is needed
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

            // Adjust fp parameters
            this->fp_prev = prevNode;
            this->fp_depth = depth + newPrefixLength;

            newNode->insertNode4(this, nodeRef,
                                 existingKey[depth + newPrefixLength], node);
            newNode->insertNode4ChangeFp(
                this, nodeRef, key[depth + newPrefixLength], makeLeaf(value));
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
                    this->fp_prev = prevNode;
                    newNode->insertNode4(this, nodeRef,
                                         node->prefix[mismatchPos], node);
                    node->prefixLength -= (mismatchPos + 1);
                    memmove(node->prefix, node->prefix + mismatchPos + 1,
                            min(node->prefixLength, maxPrefixLength));
                } else {
                    node->prefixLength -= (mismatchPos + 1);
                    uint8_t minKey[maxKeyLength];
                    loadKey(getLeafValue(minimum(node)), minKey);
                    this->fp_prev = prevNode;
                    newNode->insertNode4(this, nodeRef,
                                         minKey[depth + mismatchPos], node);
                    memmove(node->prefix, minKey + depth + mismatchPos + 1,
                            min(node->prefixLength, maxPrefixLength));
                }
                // Adjust fp_depth
                this->fp_depth = depth;
                newNode->insertNode4ChangeFp(
                    this, nodeRef, key[depth + mismatchPos], makeLeaf(value));
                return;
            }
            depth += node->prefixLength;
        }

        // Recurse
        ArtNode** child = findChild(node, key[depth]);
        if (*child) {
            insert_recursive_change_fp(*child, child, key, depth + 1, value,
                                       maxKeyLength, node);
            return;
        }

        // Insert leaf into inner node
        ArtNode* newNode = makeLeaf(value);
        this->fp_prev = prevNode;
        this->fp_depth = depth - node->prefixLength;
        switch (node->type) {
            case NodeType4:
                static_cast<Node4*>(node)->insertNode4ChangeFp(
                    this, nodeRef, key[depth], newNode);
                break;
            case NodeType16:
                static_cast<Node16*>(node)->insertNode16ChangeFp(
                    this, nodeRef, key[depth], newNode);
                break;
            case NodeType48:
                static_cast<Node48*>(node)->insertNode48ChangeFp(
                    this, nodeRef, key[depth], newNode);
                break;
            case NodeType256:
                static_cast<Node256*>(node)->insertNode256ChangeFp(
                    this, nodeRef, key[depth], newNode);
                break;
        }
    }
};

}  // namespace ART

#include "../QuArtNodeMethods.cpp"
