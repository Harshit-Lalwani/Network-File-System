#include "lru_cache.h"
#include <stdlib.h>
#include <string.h>

static unsigned int hashKey(const char *key) {
    unsigned int hash = 0;
    while (*key) {
        hash = (hash * 31) + *key;
        key++;
    }
    return hash % TABLE_SIZE;
}

LRUCache *createLRUCache(int capacity) {
    LRUCache *cache = (LRUCache *)malloc(sizeof(LRUCache));
    cache->capacity = capacity;
    cache->size = 0;
    cache->head = NULL;
    cache->tail = NULL;
    cache->hashTable = (CacheNode **)calloc(TABLE_SIZE, sizeof(CacheNode *));
    return cache;
}

void freeLRUCache(LRUCache *cache) {
    CacheNode *current = cache->head;
    while (current) {
        CacheNode *next = current->next;
        free(current->key);
        free(current);
        current = next;
    }
    free(cache->hashTable);
    free(cache);
}

static void moveToHead(LRUCache *cache, CacheNode *node) {
    if (node == cache->head) return;
    if (node->prev) node->prev->next = node->next;
    if (node->next) node->next->prev = node->prev;
    if (node == cache->tail) cache->tail = node->prev;
    node->next = cache->head;
    node->prev = NULL;
    if (cache->head) cache->head->prev = node;
    cache->head = node;
    if (!cache->tail) cache->tail = node;
}

static void removeTail(LRUCache *cache) {
    if (!cache->tail) return;
    CacheNode *tail = cache->tail;
    if (tail->prev) tail->prev->next = NULL;
    cache->tail = tail->prev;
    if (cache->tail == NULL) cache->head = NULL;
    unsigned int index = hashKey(tail->key);
    cache->hashTable[index] = NULL;
    free(tail->key);
    free(tail);
    cache->size--;
}

Node *getLRUCache(LRUCache *cache, const char *key) {
    unsigned int index = hashKey(key);
    CacheNode *node = cache->hashTable[index];
    while (node) {
        if (strcmp(node->key, key) == 0) {
            moveToHead(cache, node);
            return node->node;
        }
        node = node->next;
    }
    return NULL;
}

void putLRUCache(LRUCache *cache, const char *key, Node *node) {
    unsigned int index = hashKey(key);
    CacheNode *existingNode = cache->hashTable[index];
    while (existingNode) {
        if (strcmp(existingNode->key, key) == 0) {
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
    if (cache->head) cache->head->prev = newNode;
    cache->head = newNode;
    if (!cache->tail) cache->tail = newNode;
    cache->hashTable[index] = newNode;
    cache->size++;
    if (cache->size > cache->capacity) {
        removeTail(cache);
    }
}