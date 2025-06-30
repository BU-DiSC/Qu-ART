/*
 * ArtNode and its derived classes for an Adaptive Radix Tree (ART) implementation
 */

 #pragma once

 #include <assert.h>
 #include <emmintrin.h>  // x86 SSE intrinsics
 #include <immintrin.h>  // AVX512
  
 #include <stdint.h>     // integer types
 #include <stdio.h>
 #include <stdlib.h>    // malloc, free
 #include <string.h>    // memset, memcpy
 #include <sys/time.h>  // gettime
  
 #include <algorithm>  // std::random_shuffle
 #include <chrono>
 
 #include "Helper.h" 

 #include <cassert>
 #include <cstdio>
 #include <fstream>
 #include <iostream>
 #include <locale>
 #include <memory>
 #include <stdexcept>
  
 #include <array>
  
 namespace ART {   
    class ART; 
     // Constants for the node types
     static const int8_t NodeType4 = 0;
     static const int8_t NodeType16 = 1;
     static const int8_t NodeType48 = 2;
     static const int8_t NodeType256 = 3;
 
     // The maximum prefix length for compressed paths stored in the
     // header, if the path is longer it is loaded from the database on
     // demand
     static const unsigned maxPrefixLength = 4;
 
     // Shared header of all inner nodes
     struct ArtNode {
         // length of the compressed path (prefix)
         uint32_t prefixLength;
         // number of non-null children
         uint16_t count;
         // node type
         int8_t type;
         // compressed path (prefix)
         uint8_t prefix[maxPrefixLength];
 
         ArtNode(int8_t type) : prefixLength(0), count(0), type(type) {}
     };
 
     // This address is used to communicate that search failed
     ArtNode* nullNode = NULL;
     // Empty marker
     static const uint8_t emptyMarker = 48;
 
     // Node with up to 4 children
     struct Node4 : ArtNode {
         uint8_t key[4];
         ArtNode* child[4];
 
         Node4() : ArtNode(NodeType4) {
             memset(key, 0, sizeof(key));
             memset(child, 0, sizeof(child));
         }
 
         void insertNode4(ART* tree, ArtNode** nodeRef, uint8_t keyByte,
             ArtNode* child);
         void insertNode4(ART* tree, ArtNode** nodeRef, uint8_t keyByte,
             ArtNode* child, std::array<ArtNode*, maxPrefixLength> temp_fp_path,
             size_t temp_fp_path_length, size_t depth_prev);
         void eraseNode4(ART* tree, ArtNode** nodeRef, ArtNode** leafPlace);

     };
 
     // Node with up to 16 children
     struct Node16 : ArtNode {
         uint8_t key[16];
         ArtNode* child[16];
 
         Node16() : ArtNode(NodeType16) {
             memset(key, 0, sizeof(key));
             memset(child, 0, sizeof(child));
         }
 
         void insertNode16(ART* tree, ArtNode** nodeRef, uint8_t keyByte,
             ArtNode* child);
         void insertNode16(ART* tree, ArtNode** nodeRef, uint8_t keyByte,
             ArtNode* child, std::array<ArtNode*, maxPrefixLength> temp_fp_path,
             size_t temp_fp_path_length, size_t depth_prev);
         void eraseNode16(ART* tree, ArtNode** nodeRef, ArtNode** leafPlace);

     };
 
     // Node with up to 48 children
     struct Node48 : ArtNode {
         uint8_t childIndex[256];
         ArtNode* child[48];
 
         Node48() : ArtNode(NodeType48) {
             memset(childIndex, emptyMarker, sizeof(childIndex));
             memset(child, 0, sizeof(child));
         }    
 
         void insertNode48(ART* tree, ArtNode** nodeRef, uint8_t keyByte,
             ArtNode* child);
         void insertNode48(ART* tree, ArtNode** nodeRef, uint8_t keyByte,
             ArtNode* child, std::array<ArtNode*, maxPrefixLength> temp_fp_path,
             size_t temp_fp_path_length, size_t depth_prev);
         void eraseNode48(ART* tree, ArtNode** nodeRef, uint8_t keyByte);

     };
 
     // Node with up to 256 children
     struct Node256 : ArtNode {
         ArtNode* child[256];
 
         Node256() : ArtNode(NodeType256) { memset(child, 0, sizeof(child)); }
 
         void insertNode256(ART* tree, ArtNode** nodeRef, uint8_t keyByte,
             ArtNode* child);
         void insertNode256(ART* tree, ArtNode** nodeRef, uint8_t keyByte,
            ArtNode* child, std::array<ArtNode*, maxPrefixLength> temp_fp_path,
            size_t temp_fp_path_length, size_t depth_prev);
         void eraseNode256(ART* tree, ArtNode** nodeRef, uint8_t keyByte);

     };
 
     void copyPrefix(ArtNode* src, ArtNode* dst) {
         // Helper function that copies the prefix from the source to the destination
         // node
         dst->prefixLength = src->prefixLength;
         memcpy(dst->prefix, src->prefix, min(src->prefixLength, maxPrefixLength));
     }
 
     inline ArtNode* makeLeaf(uintptr_t tid) {
         // Create a pseudo-leaf
         return reinterpret_cast<ArtNode*>((tid << 1) | 1);
     }
     
     inline uintptr_t getLeafValue(ArtNode* node) {
         // The the value stored in the pseudo-leaf
         return reinterpret_cast<uintptr_t>(node) >> 1;
     }
     
     inline bool isLeaf(ArtNode* node) {
         // Is the node a leaf?
         return reinterpret_cast<uintptr_t>(node) & 1;
     }
 
     void Node4::insertNode4(ART* tree, ArtNode** nodeRef, uint8_t keyByte,
         ArtNode* child) {
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
             delete this;
             return newNode->insertNode16(tree, nodeRef, keyByte, child);
         }
     }
 
     void Node4::eraseNode4(ART* tree, ArtNode** nodeRef, ArtNode** leafPlace) {
         // Delete leaf from inner node
         unsigned pos = leafPlace - this->child;
         memmove(this->key + pos, this->key + pos + 1, this->count - pos - 1);
         memmove(this->child + pos, this->child + pos + 1,
                 (this->count - pos - 1) * sizeof(uintptr_t));
         this->count--;
 
         if (this->count == 1) {
             // Get rid of one-way node
             ArtNode* child = this->child[0];
             if (!isLeaf(child)) {
                 // Concantenate prefixes
                 unsigned l1 = this->prefixLength;
                 if (l1 < maxPrefixLength) {
                     this->prefix[l1] = this->key[0];
                     l1++;
                 }
                 if (l1 < maxPrefixLength) {
                     unsigned l2 = min(child->prefixLength, maxPrefixLength - l1);
                     memcpy(this->prefix + l1, child->prefix, l2);
                     l1 += l2;
                 }
                 // Store concantenated prefix
                 memcpy(child->prefix, this->prefix, min(l1, maxPrefixLength));
                 child->prefixLength += this->prefixLength + 1;
             }
             *nodeRef = child;
             delete this;
         }
     }
 
     void Node16::insertNode16(ART* tree, ArtNode** nodeRef, uint8_t keyByte,
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
             memcpy(newNode->child, this->child, this->count * sizeof(uintptr_t));
             for (unsigned i = 0; i < this->count; i++)
                 newNode->childIndex[flipSign(this->key[i])] = i;
             copyPrefix(this, newNode);
             newNode->count = this->count;
             delete this;
             return newNode->insertNode48(tree, nodeRef, keyByte, child);
         }
     }
     
     void Node16::eraseNode16(ART* tree, ArtNode** nodeRef, ArtNode** leafPlace) {
         // Delete leaf from inner node
         unsigned pos = leafPlace - this->child;
         memmove(this->key + pos, this->key + pos + 1, this->count - pos - 1);
         memmove(this->child + pos, this->child + pos + 1,
                 (this->count - pos - 1) * sizeof(uintptr_t));
         this->count--;
 
         if (this->count == 3) {
             // Shrink to Node4
             Node4* newNode = new Node4();
             newNode->count = this->count;
             copyPrefix(this, newNode);
             for (unsigned i = 0; i < 4; i++)
                 newNode->key[i] = flipSign(this->key[i]);
             memcpy(newNode->child, this->child, sizeof(uintptr_t) * 4);
             *nodeRef = newNode;
             delete this;
         }
     }
 
     void Node48::insertNode48(ART* tree, ArtNode** nodeRef, uint8_t keyByte, ArtNode* child) {
         // Insert leaf into inner node
         if (this->count < 48) {
             // Insert element
             unsigned pos = this->count;
             if (this->child[pos])
                 for (pos = 0; this->child[pos] != NULL; pos++);
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
             delete this;
             return newNode->insertNode256(tree, nodeRef, keyByte, child);
         }
     }
 
     void Node48::eraseNode48(ART* tree, ArtNode** nodeRef, uint8_t keyByte) {
         // Delete leaf from inner node
         this->child[this->childIndex[keyByte]] = NULL;
         this->childIndex[keyByte] = emptyMarker;
         this->count--;
 
         if (this->count == 12) {
             // Shrink to Node16
             Node16* newNode = new Node16();
             *nodeRef = newNode;
             copyPrefix(this, newNode);
             for (unsigned b = 0; b < 256; b++) {
                 if (this->childIndex[b] != emptyMarker) {
                     newNode->key[newNode->count] = flipSign(b);
                     newNode->child[newNode->count] =
                         this->child[this->childIndex[b]];
                     newNode->count++;
                 }
             }
             delete this;
         }
     }    
 
     void Node256::insertNode256(ART* tree, ArtNode** nodeRef, uint8_t keyByte,
         ArtNode* child) {
         // Insert leaf into inner node
         this->count++;
         this->child[keyByte] = child;
     }  
     

     void Node256::eraseNode256(ART* tree, ArtNode** nodeRef, uint8_t keyByte) {
         // Delete leaf from inner node
         this->child[keyByte] = NULL;
         this->count--;
 
         if (this->count == 37) {
             // Shrink to Node48
             Node48* newNode = new Node48();
             *nodeRef = newNode;
             copyPrefix(this, newNode);
             for (unsigned b = 0; b < 256; b++) {
                 if (this->child[b]) {
                     newNode->childIndex[b] = newNode->count;
                     newNode->child[newNode->count] = this->child[b];
                     newNode->count++;
                 }
             }
             delete this;
         }
     }   
 
 
     ArtNode** findChild(ArtNode* n, uint8_t keyByte) {
         // Find the next child for the keyByte
         switch (n->type) {
             case NodeType4: {
                 Node4* node = static_cast<Node4*>(n);
                 for (unsigned i = 0; i < node->count; i++)
                     if (node->key[i] == keyByte) return &node->child[i];
                 return &nullNode;
             }
             case NodeType16: {
                 Node16* node = static_cast<Node16*>(n);
                 __m128i cmp = _mm_cmpeq_epi8(
                     _mm_set1_epi8(flipSign(keyByte)),
                     _mm_loadu_si128(reinterpret_cast<__m128i*>(node->key)));
                 unsigned bitfield =
                     _mm_movemask_epi8(cmp) & ((1 << node->count) - 1);
                 if (bitfield)
                     return &node->child[ctz(bitfield)];
                 else
                     return &nullNode;
             }
             case NodeType48: {
                 Node48* node = static_cast<Node48*>(n);
                 if (node->childIndex[keyByte] != emptyMarker)
                     return &node->child[node->childIndex[keyByte]];
                 else
                     return &nullNode;
             }
             case NodeType256: {
                 Node256* node = static_cast<Node256*>(n);
                 return &(node->child[keyByte]);
             }
         }
         throw;  // Unreachable
     }
     
     
     ArtNode* minimum(ArtNode* node) {
         // Find the leaf with smallest key
         if (!node) return NULL;
     
         if (isLeaf(node)) return node;
     
         switch (node->type) {
             case NodeType4: {
                 Node4* n = static_cast<Node4*>(node);
                 return minimum(n->child[0]);
             }
             case NodeType16: {
                 Node16* n = static_cast<Node16*>(node);
                 return minimum(n->child[0]);
             }
             case NodeType48: {
                 Node48* n = static_cast<Node48*>(node);
                 unsigned pos = 0;
                 while (n->childIndex[pos] == emptyMarker) pos++;
                 return minimum(n->child[n->childIndex[pos]]);
             }
             case NodeType256: {
                 Node256* n = static_cast<Node256*>(node);
                 unsigned pos = 0;
                 while (!n->child[pos]) pos++;
                 return minimum(n->child[pos]);
             }
         }
         throw;  // Unreachable
     }
     
     ArtNode* maximum(ArtNode* node) {
         // Find the leaf with largest key
         if (!node) return NULL;
     
         if (isLeaf(node)) return node;
     
         switch (node->type) {
             case NodeType4: {
                 Node4* n = static_cast<Node4*>(node);
                 return maximum(n->child[n->count - 1]);
             }
             case NodeType16: {
                 Node16* n = static_cast<Node16*>(node);
                 return maximum(n->child[n->count - 1]);
             }
             case NodeType48: {
                 Node48* n = static_cast<Node48*>(node);
                 unsigned pos = 255;
                 while (n->childIndex[pos] == emptyMarker) pos--;
                 return maximum(n->child[n->childIndex[pos]]);
             }
             case NodeType256: {
                 Node256* n = static_cast<Node256*>(node);
                 unsigned pos = 255;
                 while (!n->child[pos]) pos--;
                 return maximum(n->child[pos]);
             }
         }
         throw;  // Unreachable
     }
     
     bool leafMatches(ArtNode* leaf, uint8_t key[], unsigned keyLength,
                      unsigned depth, unsigned maxKeyLength) {
         // Check if the key of the leaf is equal to the searched key
         if (depth != keyLength) {
             uint8_t leafKey[maxKeyLength];
             loadKey(getLeafValue(leaf), leafKey);
             for (unsigned i = depth; i < keyLength; i++)
                 if (leafKey[i] != key[i]) return false;
         }
         return true;
     }
     
     unsigned prefixMismatch(ArtNode* node, uint8_t key[], unsigned depth,
                             unsigned maxKeyLength) {
         // Compare the key with the prefix of the node, return the number matching
         // bytes
         unsigned pos;
         if (node->prefixLength > maxPrefixLength) {
             for (pos = 0; pos < maxPrefixLength; pos++)
                 if (key[depth + pos] != node->prefix[pos]) return pos;
             uint8_t minKey[maxKeyLength];
             loadKey(getLeafValue(minimum(node)), minKey);
             for (; pos < node->prefixLength; pos++)
                 if (key[depth + pos] != minKey[depth + pos]) return pos;
         } else {
             for (pos = 0; pos < node->prefixLength; pos++)
                 if (key[depth + pos] != node->prefix[pos]) return pos;
         }
         return pos;
     }
     
     ArtNode* lookupPessimistic(ArtNode* node, uint8_t key[], unsigned keyLength,
                                unsigned depth, unsigned maxKeyLength) {
         // Find the node with a matching key, alternative pessimistic version
     
         while (node != NULL) {
             if (isLeaf(node)) {
                 if (leafMatches(node, key, keyLength, depth, maxKeyLength))
                     return node;
                 return NULL;
             }
     
             if (prefixMismatch(node, key, depth, maxKeyLength) !=
                 node->prefixLength)
                 return NULL;
             else
                 depth += node->prefixLength;
     
             node = *findChild(node, key[depth]);
             depth++;
         }
     
         return NULL;
     }    

     void printTailPath(std::array<ArtNode*, maxPrefixLength> path, size_t path_length) {
        // Print the fp path for debugging
        for (size_t i = 0; i < path_length; i++) {
            if (isLeaf(path[i])) {
                printf("Leaf(%lu)\n", getLeafValue(path[i]));
            } 
            else {
                switch (path[i]->type) {
                case NodeType4:
                    printf("Node4 %p\n", path[i]);
                    break;
                case NodeType16:
                    printf("Node16 %p\n", path[i]);
                    break;
                case NodeType48:
                    printf("Node48 %p\n", path[i]);
                    break;
                case NodeType256:
                    printf("Node256 %p\n", path[i]);
                    break;
                default:
                    printf("Unknown NodeType %p\n", path[i]);
                    break;
                }
            }
        }
    }

 
 }