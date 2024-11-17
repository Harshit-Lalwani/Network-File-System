#ifndef LRU_CACHE_H
#define LRU_CACHE_H

#include "header.h"

typedef struct CacheNode {
    Node *node;
    char *key;
    struct CacheNode *prev;
    struct CacheNode *next;
} CacheNode;

typedef struct LRUCache {
    int capacity;
    int size;
    CacheNode *head;
    CacheNode *tail;
    CacheNode **hashTable;
} LRUCache;

LRUCache *createLRUCache(int capacity);
void freeLRUCache(LRUCache *cache);
Node *getLRUCache(LRUCache *cache, const char *key);
void putLRUCache(LRUCache *cache, const char *key, Node *node);

#endif // LRU_CACHE_H