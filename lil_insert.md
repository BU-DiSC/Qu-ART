# QuART_lil Insertion Algorithm
## Metadata
Class QuART_lil represents a QuART tree with lil implementation. It uses the following metadata to perform lil insertions:  

- __root__: *The root node of the tree*
- __maxKeyLength__: *The maximum possible length of a key in this tree*
- __fp__: *The node at the end of the fast path; the last internal node inserted to*
- __fp_ref__: *A reference to __fp__ within the tree*
- __fp_path__: *An array of the internal nodes along the path from the __root__ to __fp__; size is the maximum prefix length*
- __fp_path_ref__: *An array of references to the nodes of __fp_path__; size is the maximum prefix length*
- __fp_path_length__: *The current length of the fast path; can act as an index to the last item in __fp_path__/__fp_path_ref__*
- __fp_leaf__: *The last leaf inserted to the tree*
- __fp_depth__: *The prefix depth of __fp__*

## Functions

### canLilInsert
#### Description
Determines if a key fits on the end of the current fast path.
#### Arguments  
- __uint8_t key[]__: *The key of some value to insert*  

#### Algorithm  
- If the __root__ is null or a leaf (if the tree is empty or has one value) then return false.
    - There is not currently a fast path to insert to as there are no internal nodes.
- Extract the value of __fp_leaf__ and use it to determine a corresponding key array.
- Compare that array to __key__ using memcmp.
    - If they do not match at some point, they must belong at different parts of the tree and a fast path insert must not be viable. Return false.
    - Else, they must share a path and the fast path insert is viable. Return True.  

### insert (public)
#### Description
Public-facing insert function that calls the recursive __insert__ function.
#### Arguments  
- __uint8_t__ key[]: *The key of the value to be inserted*
- __uintptr_t__ value: *The value to be inserted*


#### Algorithm  
- If __canLilInsert(key)__ returns true and __fp__ does not have enough children to fill its capacity, __value__ can be inserted on the fast path.
- If inserting to the fast path, call __insert(this, *fp_ref, fp_ref, key, fp_depth, value, maxPrefixLength, true)__.
    - Insert starting from __fp__ instead of from __root__.
- Else, set fast path metadata to include only the root and call __insert(this, root, &root, key, 0, value, maxPrefixLength, true)__.
    - The new fast path will be built as this insert is processed so the relevant metadata needs to be set to just the root to prepare for this.
        - Set all values in __fp_path__ and __fp_path_ref__ but the first to be null.
        - Set __fp_path_length__ to 1.
        - Set __fp_depth__ to the length of the __root__ prefix.
        - Set __fp__ to be __root__.
        - Set __fp_ref__ to be a reference to __root__.
        - Set __fp_leaf__ to be null.
    - Then, perform a normal tree insertion starting at the __root__.

### insert (private, recursive)
#### Description
Private function that recursively traverses the tree and performs logic for inserting new values. 
#### Arguments
- __ArtNode* node__: *The internal node of the tree being inserted into in this call*
- __ArtNode** nodeRef__: *The pointer to __node__ within the tree*
- __uint8_t key[]__: *The key of the value being inserted*
- __unsigned depth__: *The prefix depth of __node__*
- __uintptr_t value__: *The value being inserted*
- __bool firstCall__: *Indicates if this recursive call is the first in the series*

#### Algorithm
- If __node__ is null:
    - Create a new leaf for __value__ and assign the value at __nodeRef__ to this leaf.
    - Return.
    - *No changes are made to the fast path as, if __node__ is null, then the tree was previously empty and has no internal nodes to compose a fast path yet.*
- If __node__ is a leaf:
    - Create a new node4 with a prefix of the common sections of both __key__ and the key of __node__.
    - Create a new leaf for __value__, then insert it and __node__ into the new node4 as children.
    - Update __root__ and the value referenced by __nodeRef__ to be the new node4.
        - *Since the current root is a leaf, it must be that this node4 will be the only internal node in the tree and thus must be the new root.*
    - Update fast path metadata.
        - Update __fp__ to be __root__ and __fp_ref__ to be __nodeRef__.
        - Update the value of __fp_path[0]__ to be __root__ and __fp_path_ref[0]__ to be __nodeRef__.
        - Set __fp_path_length__ to 1.
        - Set __fp_depth__ to be the length of the __root__ prefix.
        - Set __fp_leaf__ to be the leaf created from __value__.
    - Return.
- If __node__ has a prefix:
    - If the prefix of __node__ does not match the section of __key__ corresponding to the current __depth__:
        - *This condition would mean that __key__ does not fit into the current __node__ and cannot be inserted into it or one of its children. A new branch needs to be added to accomodate the new __key__.*
        - Create a new node4 with a prefix of the common sections of both __key__ and the key of __node__. Set the value of __nodeRef__ to be the new node4.
        - Replace the prefix of __node__ with its current prefix minus the section that is now the prefix of the new node4.
        - Create a new leaf for __value__, then insert it and __node__ into the new node4 as children.
        - Update fast path metadata.
            - Update __fp__ to be the new node4.
            - Update __fp_ref__ to be __nodeRef__.
            - Update the value of __fp_path[fp_path_length]__ to be __fp__ and __fp_path_ref[fp_path_length]__ to be __fp_ref__.
            - Increment __fp_path_length__ by 1.
            - Increment __fp_depth__ by the length of the new node4 prefix.
            - Set __fp_leaf__ to be the leaf created from __value__.
        - Return.
    - Increment __depth__ by the length of the prefix of __node__.
- If __firstCall__ is false:
    - *The first recursive call will always be to a node already on the end of the fast path. In order to avoid double-counting it on the fast path, only update the fast path on subsequent recursive calls in the series.*
    - Update the fast path metadata.
        - Update __fp__ to be __node__.
        - Update __fp_ref__ to be __nodeRef__.
        - Update the value of __fp_path[fp_path_length]__ to be __fp__ and __fp_path_ref[fp_path_length]__ to be __fp_ref__.
        - Increment __fp_path_length__ by 1.
        - Increment __fp_depth__ by the length of the __node__ prefix.
- Check for a child internal node in __node__.
- If __node__ has a child internal node (designated as "child"):
    - Call __insert(this, child, reference to child, key, depth + 1, value, maxKeyLength, false)__.
    - Return.
- *If __node__ is an internal node and its prefix matches the corresponding portion of __key__ then the leaf for __value__ must belong on the same path as __node__. If this is true and __node__ has no internal node children that __key__ might possibly be inserted into then the leaf for __value__ can only be inserted into __node__.*
- Create a new leaf for __value__ and set __fp_leaf__ to be this leaf.
- Call __insertNodeX(this, nodeRef, key[depth], _fp_leaf_)__ where X is the size of __node__ (4, 16, 48, or 256).