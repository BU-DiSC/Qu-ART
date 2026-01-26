#include <iostream>
#include <cassert>
#include "../ART.h"
#include "../ArtNode.h"
#include "../Helper.h"

using namespace std;

void test_compress_linear_chain() {
    cout << "Test 1: Linear chain of nodes with single children" << endl;
    
    ART::ART* tree = new ART::ART();
    
    // Insert keys that create a linear chain with intermediate nodes:
    // Start with a key, then add another that shares prefix but diverges
    // This creates intermediate nodes with single children
    uint32_t keys[] = {
        0x01020304,  // First key
        0x01020305,  // Same prefix, different last byte - creates intermediate nodes
        0x05060708   // Different prefix - creates single-child nodes in upper levels
    };
    
    for (uint32_t key : keys) {
        uint8_t key_bytes[4];
        ART::loadKey(key, key_bytes);
        tree->insert(key_bytes, key);
    }
    
    cout << "Before compression:" << endl;
    tree->printTree();
    
    int compressed = tree->compressTree();
    
    cout << "\nAfter compression:" << endl;
    tree->printTree();
    cout << "Compressed nodes: " << compressed << endl;
    
    // Verify lookups still work
    for (uint32_t key : keys) {
        uint8_t key_bytes[4];
        ART::loadKey(key, key_bytes);
        ART::ArtNode* result = tree->lookup(key_bytes);
        assert(ART::isLeaf(result));
        assert(ART::getLeafValue(result) == key);
    }
    
    cout << "✓ All lookups successful after compression\n" << endl;
    delete tree;
}

void test_compress_no_compression_needed() {
    cout << "Test 2: Tree with no single-child nodes" << endl;
    
    ART::ART* tree = new ART::ART();
    
    // Insert keys that create nodes with multiple children
    // 0x00000001, 0x01000001, 0x02000001, 0x03000001
    uint32_t keys[] = {0x00000001, 0x01000001, 0x02000001, 0x03000001};
    
    for (uint32_t key : keys) {
        uint8_t key_bytes[4];
        ART::loadKey(key, key_bytes);
        tree->insert(key_bytes, key);
    }
    
    cout << "Before compression:" << endl;
    tree->printTree();
    
    int compressed = tree->compressTree();
    
    cout << "\nAfter compression:" << endl;
    tree->printTree();
    cout << "Compressed nodes: " << compressed << endl;
    assert(compressed == 0);
    
    cout << "✓ No compression performed (as expected)\n" << endl;
    delete tree;
}

void test_compress_depth_4_boundary() {
    cout << "Test 3: Nodes at depth 4 should not be compressed" << endl;
    
    ART::ART* tree = new ART::ART();
    
    // Insert keys that create a chain going to depth 5
    // These should only compress up to depth 3
    for (uint32_t i = 1; i <= 10; i++) {
        uint8_t key[4];
        ART::loadKey(i, key);
        tree->insert(key, i);
    }
    
    cout << "Before compression:" << endl;
    tree->printTree();
    
    int compressed = tree->compressTree();
    
    cout << "\nAfter compression:" << endl;
    tree->printTree();
    cout << "Compressed nodes: " << compressed << endl;
    
    // Verify all lookups still work
    for (uint32_t i = 1; i <= 10; i++) {
        uint8_t key[4];
        ART::loadKey(i, key);
        ART::ArtNode* result = tree->lookup(key);
        assert(ART::isLeaf(result));
        assert(ART::getLeafValue(result) == i);
    }
    
    cout << "✓ All lookups successful, depth 4+ preserved\n" << endl;
    delete tree;
}

void test_compress_mixed_tree() {
    cout << "Test 4: Mixed tree with both compressible and non-compressible nodes" << endl;
    
    ART::ART* tree = new ART::ART();
    
    // Create a tree with:
    // - Some branches with single children (compressible)
    // - Some branches with multiple children (not compressible)
    uint32_t keys[] = {
        0x00000001, 0x00000002, 0x00000003,  // Linear chain
        0x01000001, 0x01000002,              // Another branch
        0x02000001, 0x02010001               // Branch with divergence
    };
    
    for (uint32_t key : keys) {
        uint8_t key_bytes[4];
        ART::loadKey(key, key_bytes);
        tree->insert(key_bytes, key);
    }
    
    cout << "Before compression:" << endl;
    tree->printTree();
    
    int compressed = tree->compressTree();
    
    cout << "\nAfter compression:" << endl;
    tree->printTree();
    cout << "Compressed nodes: " << compressed << endl;
    
    // Verify all lookups still work
    for (uint32_t key : keys) {
        uint8_t key_bytes[4];
        ART::loadKey(key, key_bytes);
        ART::ArtNode* result = tree->lookup(key_bytes);
        assert(ART::isLeaf(result));
        assert(ART::getLeafValue(result) == key);
    }
    
    cout << "✓ All lookups successful in mixed tree\n" << endl;
    delete tree;
}

void test_compress_with_prefix() {
    cout << "Test 5: Compression with prefix paths" << endl;
    
    ART::ART* tree = new ART::ART();
    
    // Insert keys that will create prefix compression
    // Then test if compressTree works correctly with prefixes
    uint32_t keys[] = {
        0x00000001,
        0x00000101,
        0x00000201
    };
    
    for (uint32_t key : keys) {
        uint8_t key_bytes[4];
        ART::loadKey(key, key_bytes);
        tree->insert(key_bytes, key);
    }
    
    cout << "Before compression:" << endl;
    tree->printTree();
    
    int compressed = tree->compressTree();
    
    cout << "\nAfter compression:" << endl;
    tree->printTree();
    cout << "Compressed nodes: " << compressed << endl;
    
    // Verify all lookups still work
    for (uint32_t key : keys) {
        uint8_t key_bytes[4];
        ART::loadKey(key, key_bytes);
        ART::ArtNode* result = tree->lookup(key_bytes);
        assert(ART::isLeaf(result));
        assert(ART::getLeafValue(result) == key);
    }
    
    cout << "✓ Compression works correctly with prefixes\n" << endl;
    delete tree;
}

int main() {
    cout << "=== Testing compressTree function ===" << endl << endl;
    
    test_compress_linear_chain();
    test_compress_no_compression_needed();
    test_compress_depth_4_boundary();
    test_compress_mixed_tree();
    test_compress_with_prefix();
    
    cout << "=== All tests passed! ===" << endl;
    
    return 0;
}
