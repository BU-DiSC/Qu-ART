# QuART_lil Insertion Algorithm
## Metadata
Class QuART_lil represents a QuART tree with lil implementation. It uses the following metadata to perform lil insertions:  

- __root__: *the root node of the tree*
- __maxKeyLength__: *the maximum possible length of a key in this tree*
- __fp__: *the node at the end of the fast path; the last internal node inserted to*
- __fp_ref__: *a reference to __fp__ within the tree*
- __fp_path__: *an array of the internal nodes along the path from the __root__ to __fp__; size is the maximum prefix length*
- __fp_path_ref__: *an array of references to the nodes of __fp_path__; size is the maximum prefix length*
- __fp_path_length__: *the current length of the fast path; can act as an index to the last item in __fp_path__/__fp_path_ref__*
- __fp_leaf__: *the last leaf inserted to the tree*
- __fp_depth__: *the prefix depth of __fp__*

## Functions

### canLilInsert
#### Arguments  
- __uint8_t key[]__: *the key of some value to insert*  

#### Algorithm  
- If the __root__ is null or a leaf (if the tree is empty or has one value) then return false
    - There is not currently a fast path to insert to as there are no internal nodes
- Extract the value of __fp_leaf__ and use it to determine a corresponding key array, leafArr.
- Compare leafArr to key using memcmp.
    - If they do not match at some point, they must belong at different parts of the tree and a fast path insert is not viable.
    - Else, they must share a path and the fast path insert is viable.  

### insert (public)
#### Arguments  
- __uint8_t__ key[]: *the key of the value to be inserted*
- __uintptr_t__ value: *the value to be inserted*


#### Algorithm  
- If __canLilInsert(key)__ returns true and __fp__ is not full, __value__ can be inserted on the fast path.
- If inserting to the fast path, call __insert(this, *fp_ref, fp_ref, key, fp_depth, value, maxPrefixLength)__
    - Insert starting from __fp__ instead of from __root__
- Else, reset fast path metadata and call __insert(this, root, &root, key, 0, value, maxPrefixLength)__
    - The new fast path will be built as this insert is processed so the relevant metadata needs to be reset in preparation for this.
        - Set all values in __fp_path__ and __fp_path_ref__ to be null.
        - Set __fp_path_length__ and __fp_depth__ to 0.
        - Set __fp__, __fp_ref__, and __fp_leaf__ to be null
    - Then, perform a normal tree insertion starting at the __root__

### insert (private, recursive)
#### Arguments
- __ArtNode* node__: *the internal node of the tree being inserted into in this call*
- __ArtNode** nodeRef__: *the pointer to __node__ within the tree*
- __uint8_t key[]__: *the key of the value being inserted*
- __unsigned depth__: *the prefix depth of __node__*
- __uintptr_t value__: *the value being inserted*

#### Algorithm
- If __node__ is null:
    - Create a new leaf for __value__ and assign the value at __nodeRef__ to this leaf
    - Return
    - *No changes are made to the fast path as, if __node__ is null, then the tree was previously empty and has no internal nodes to compose a fast path yet*
- If __node__ is a leaf:
    - Create a new node4 with a prefix of the common sections of both __key__ and the key of __node__
    - create a new leaf for __value__, then insert it and __node__ into the new node4 as children
    - Update __root__ and the value referenced by __nodeRef__ to be the new node4
        - *Since the current root is a leaf, it must be that this node4 will be the only internal node in the tree and thus must be the root.*
    - Update fast path metadata.
        - Update __fp__ to be __root__ and __fp_ref__ to be __nodeRef__.
        - Update the value of __fp_path[0]__ to be __root__ and __fp_path_ref[0]__ to be __nodeRef__.
        - Set __fp_path_length__ to 1.
        - Set __fp_depth__ to be the length of the node4 prefix.
        - Set __fp_leaf__ to be the leaf created from __value__
    - Return
