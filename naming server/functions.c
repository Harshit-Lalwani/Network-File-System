#include "header.h"

Node *receiveNodeChain(int sock)
{
    Node *head = NULL;
    Node *current = NULL;

    while (1)
    {
        // Check for end of chain
        int marker;
        if (recv(sock, &marker, sizeof(int), 0) <= 0)
            return NULL;
        send(sock, "OK", 2, 0);
        if (marker == -1)
            break; // End of chain

        // Receive node data
        int name_len;
        if (recv(sock, &name_len, sizeof(int), 0) <= 0)
            return NULL;
        send(sock, "OK", 2, 0);

        char *name = malloc(name_len);
        if (recv(sock, name, name_len, 0) <= 0)
        {
            free(name);
            return NULL;
        }
        send(sock, "OK", 2, 0);

        NodeType type;
        Permissions permissions;
        if (recv(sock, &type, sizeof(NodeType), 0) <= 0)
        {
            free(name);
            return NULL;
        }
        send(sock, "OK", 2, 0);

        if (recv(sock, &permissions, sizeof(Permissions), 0) <= 0)
        {
            free(name);
            return NULL;
        }
        send(sock, "OK", 2, 0);

        int loc_len;
        if (recv(sock, &loc_len, sizeof(int), 0) <= 0)
        {
            free(name);
            return NULL;
        }
        send(sock, "OK", 2, 0);

        char *dataLocation = malloc(loc_len);
        if (recv(sock, dataLocation, loc_len, 0) <= 0)
        {
            free(name);
            free(dataLocation);
            return NULL;
        }
        send(sock, "OK", 2, 0);

        // Create new node
        Node *newNode = createNode(name, type, permissions, dataLocation);
        free(name);
        free(dataLocation);

        // Check if node has children
        int has_children;
        if (recv(sock, &has_children, sizeof(int), 0) <= 0)
        {
            freeNode(newNode);
            return NULL;
        }
        send(sock, "OK", 2, 0);

        if (has_children)
        {
            // Create hash table for children
            newNode->children = createNodeTable();

            // Receive all hash table entries
            for (int i = 0; i < TABLE_SIZE; i++)
            {
                newNode->children->table[i] = receiveNodeChain(sock);

                // Set parent pointers for the chain
                Node *child = newNode->children->table[i];
                while (child != NULL)
                {
                    child->parent = newNode;
                    child = child->next;
                }
            }
        }

        // Add to chain
        if (head == NULL)
        {
            head = newNode;
            current = head;
        }
        else
        {
            current->next = newNode;
            current = newNode;
        }
    }

    return head;
}
void recursiveList(Node *node, const char *current_path, char *response, int *response_offset, size_t response_size)
{
    if (!node)
        return;

    // char full_path[1024];
    // Construct the path for the current node
    char new_path[1024];
    if (strcmp(current_path, "") == 0)
    {
        // If current_path is empty, use only the node's name
        snprintf(new_path, sizeof(new_path), "%s", node->name);
    }
    else
    {
        // Check if node->name causes duplication with current_path
        if (strstr(current_path, node->name) && strstr(current_path, node->name) + strlen(node->name) == current_path + strlen(current_path))
        {
            // If current_path already ends with node->name, don't append it
            snprintf(new_path, sizeof(new_path), "%s", current_path);
        }
        else
        {
            // Otherwise, append node->name to current_path
            snprintf(new_path, sizeof(new_path), "%s/%s", current_path, node->name);
        }
    }
    // Append current node's information to the response
    *response_offset += snprintf(response + *response_offset, response_size - *response_offset,
                                 "Path: %s, Type: %s\n",
                                 new_path,
                                 (node->type == FILE_NODE ? "File" : "Directory"));

    // If the node is a directory, traverse its children
    if (node->type == DIRECTORY_NODE)
    {
        NodeTable *children = node->children; // Directly use node->children without '&'
        for (int i = 0; i < TABLE_SIZE; i++)
        {
            Node *child_node = children->table[i];
            while (child_node)
            {
                recursiveList(child_node, new_path, response, response_offset, response_size);
                child_node = child_node->next;
            }
        }
    }
}

StorageServerList *findStorageServersByPath_List(StorageServerTable *table, const char *path)
{
    StorageServerList *matching_servers = NULL;
    StorageServerList *last_match = NULL;

    for (int i = 0; i < TABLE_SIZE; i++)
    {
        pthread_mutex_lock(&table->locks[i]);
        StorageServer *server = table->table[i];

        while (server)
        {
            if (server->active && server->root)
            {
                // Use searchPath to check if this server has the path
                Node *found_node = searchPath(server->root, path);
                if (found_node != NULL)
                {
                    // We found the path in this server, add to the list
                    StorageServerList *new_match = (StorageServerList *)malloc(sizeof(StorageServerList));
                    new_match->server = server;
                    new_match->next = NULL;

                    if (last_match)
                    {
                        last_match->next = new_match;
                    }
                    else
                    {
                        matching_servers = new_match;
                    }
                    last_match = new_match;
                }
            }
            server = server->next;
        }
        pthread_mutex_unlock(&table->locks[i]);
    }

    return matching_servers;
}

Node *findNode(Node *root, const char *path)
{
    if (!root || !path || strlen(path) == 0)
    {
        printf("Error: Invalid root or path.\n");
        return NULL;
    }

    // Handle root path case
    if (strcmp(path, "/") == 0)
    {
        return root;
    }

    // Tokenize the path using the path separator
    char *pathCopy = strdup(path); // Make a mutable copy of the path
    char *token = strtok(pathCopy, PATH_SEPARATOR);
    Node *current = root;

    while (token != NULL)
    {
        // Traverse the children of the current node
        NodeTable *childrenTable = current->children;
        if (!childrenTable)
        {
            printf("Error: Path component '%s' not found (no children).\n", token);
            free(pathCopy);
            return NULL;
        }

        Node *child = NULL;
        for (int i = 0; i < TABLE_SIZE; i++)
        {
            child = childrenTable->table[i];
            while (child != NULL)
            {
                if (strcmp(child->name, token) == 0)
                {
                    break;
                }
                child = child->next;
            }
            if (child)
            {
                break;
            }
        }

        if (!child)
        {
            printf("Error: Path component '%s' not found.\n", token);
            free(pathCopy);
            return NULL;
        }

        // Move to the next level
        current = child;
        token = strtok(NULL, PATH_SEPARATOR);
    }

    free(pathCopy);
    return current;
}

static unsigned int hashKey(const char *key)
{
    unsigned int hash = 0;
    while (*key)
    {
        hash = (hash * 31) + *key;
        key++;
    }
    return hash % TABLE_SIZE;
}

LRUCache *createLRUCache(int capacity)
{
    LRUCache *cache = (LRUCache *)malloc(sizeof(LRUCache));
    cache->capacity = capacity;
    cache->size = 0;
    cache->head = NULL;
    cache->tail = NULL;
    cache->hashTable = (CacheNode **)calloc(TABLE_SIZE, sizeof(CacheNode *));
    return cache;
}

void freeLRUCache(LRUCache *cache)
{
    CacheNode *current = cache->head;
    while (current)
    {
        CacheNode *next = current->next;
        free(current->key);
        free(current);
        current = next;
    }
    free(cache->hashTable);
    free(cache);
}

static void moveToHead(LRUCache *cache, CacheNode *node)
{
    if (node == cache->head)
        return;
    if (node->prev)
        node->prev->next = node->next;
    if (node->next)
        node->next->prev = node->prev;
    if (node == cache->tail)
        cache->tail = node->prev;
    node->next = cache->head;
    node->prev = NULL;
    if (cache->head)
        cache->head->prev = node;
    cache->head = node;
    if (!cache->tail)
        cache->tail = node;
}

static void removeTail(LRUCache *cache)
{
    if (!cache->tail)
        return;
    CacheNode *tail = cache->tail;
    if (tail->prev)
        tail->prev->next = NULL;
    cache->tail = tail->prev;
    if (cache->tail == NULL)
        cache->head = NULL;
    unsigned int index = hashKey(tail->key);
    cache->hashTable[index] = NULL;
    free(tail->key);
    free(tail);
    cache->size--;
}

Node *getLRUCache(LRUCache *cache, const char *key)
{
    unsigned int index = hashKey(key);
    CacheNode *node = cache->hashTable[index];
    while (node)
    {
        if (strcmp(node->key, key) == 0)
        {
            moveToHead(cache, node);
            return node->node;
        }
        node = node->next;
    }
    return NULL;
}

void putLRUCache(LRUCache *cache, const char *key, Node *node)
{
    unsigned int index = hashKey(key);
    CacheNode *existingNode = cache->hashTable[index];
    while (existingNode)
    {
        if (strcmp(existingNode->key, key) == 0)
        {
            existingNode->node = node;
            moveToHead(cache, existingNode);
            return;
        }
        existingNode = existingNode->next;
    }
    CacheNode *newNode = (CacheNode *)malloc(sizeof(CacheNode));
    newNode->node = node;
    newNode->key = strdup(key);
    newNode->prev = NULL;
    newNode->next = cache->head;
    if (cache->head)
        cache->head->prev = newNode;
    cache->head = newNode;
    if (!cache->tail)
        cache->tail = newNode;
    cache->hashTable[index] = newNode;
    cache->size++;
    if (cache->size > cache->capacity)
    {
        removeTail(cache);
    }
}