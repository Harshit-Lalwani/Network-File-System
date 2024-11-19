## LRU caching
Components
CacheNode Structure:

Node *node: Pointer to the node being cached.
char *key: Key associated with the node, used for quick lookup.
CacheNode *prev: Pointer to the previous node in the doubly linked list.
CacheNode *next: Pointer to the next node in the doubly linked list.
LRUCache Structure:

int capacity: Maximum number of nodes the cache can hold.
int size: Current number of nodes in the cache.
CacheNode *head: Pointer to the most recently used node.
CacheNode *tail: Pointer to the least recently used node.
CacheNode **hashTable: Array of pointers to cache nodes for quick lookup.
Functions
hashKey:

Computes a hash value for a given key using a simple hash function.
The hash value is used to index into the hash table.
createLRUCache:

Allocates and initializes an LRU cache with a specified capacity.
Initializes the head and tail pointers to NULL and allocates memory for the hash table.
freeLRUCache:

Frees all memory associated with the LRU cache, including the cache nodes and the hash table.
moveToHead:

Moves a given cache node to the head of the doubly linked list.
This operation marks the node as the most recently used.
removeTail:

Removes the tail node (least recently used) from the doubly linked list.
Updates the hash table and reduces the cache size.
getLRUCache:

Retrieves a node from the cache using the given key.
If the node is found, it is moved to the head of the list to mark it as recently used.
Returns the node if found, otherwise returns NULL.
putLRUCache:

Adds a new node to the cache or updates an existing node.
If the node already exists, it is updated and moved to the head of the list.
If the node does not exist, a new cache node is created and added to the head of the list.
If the cache exceeds its capacity, the least recently used node is removed.
How It Works
Initialization: The cache is initialized with a specified capacity. The head and tail pointers are set to NULL, and the hash table is allocated.
Adding Nodes: When a node is added to the cache, it is placed at the head of the doubly linked list. If the cache exceeds its capacity, the tail node is removed.
Accessing Nodes: When a node is accessed, it is moved to the head of the list to mark it as recently used. This ensures that the most recently accessed nodes are always at the head of the list.
Eviction: When the cache exceeds its capacity, the least recently used node (tail) is removed to make space for new nodes.
This LRU cache implementation ensures efficient access and management of recently used nodes, improving the performance of the naming server by reducing the need to repeatedly search for nodes in the storage servers.


- Assumption:
Also for asynchronous write partial writes handling case , wheneve a client ask for performing the asyn write operation, will send him an apk when the write request will be started  , if asyn write is done then client will receive completed apk , if supppose storage server will go down , then he wont receive the completed apk , so i have a thread that check the time diff between the apk started and completed , if its taking more than 5 sec , i am assuming that ss goes offline and will according infrom the client