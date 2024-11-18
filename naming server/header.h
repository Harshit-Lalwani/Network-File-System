#ifndef HEADER_H
#define HEADER_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <linux/limits.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <asm-generic/socket.h>
#include<stdbool.h>
#include <pthread.h>
#include <ctype.h>
#define TABLE_SIZE 10
#define MAX_COMMAND_LENGTH 10
#define MAX_PATH_LENGTH 1024
#define MAX_CONTENT_LENGTH 4096
#define CHUNK_SIZE 1024
#define MAX_CLIENTS 10
#define STORAGE_PORT 8080
#define NAMING_PORT 8081
#define MAX_BUFFER_SIZE 100001
#define PATH_SEPARATOR "/"
#define LOG_FILE "naming_server.log"

#define ACK_RECEIVE_PORT 9091 // Dedicated port for receiving ACKs

#define ACK_PORT 8090 // Port for sending acknowledgment to naming server

typedef enum
{
    CMD_READ,
    CMD_WRITE,
    CMD_META,
    CMD_STREAM,
    CMD_CREATE,
    CMD_DELETE,
    CMD_COPY,
    CMD_UNKNOWN
} CommandType;

typedef enum
{
    READ = 1 << 0,
    WRITE = 1 << 1,
    EXECUTE = 1 << 2,
    APPEND = 1 << 3
} Permissions;

typedef enum
{
    FILE_NODE,
    DIRECTORY_NODE
} NodeType;

typedef struct Node
{
    char *name;      
    NodeType type;
    Permissions permissions;
    char *dataLocation;
    struct Node *parent;
    struct Node *next;
    struct NodeTable *children; 
    int lock_type; // 0= none, 1 = read, 2 = write
} Node;

typedef struct StorageServer
{
    char ip[16];
    int nm_port;
    int client_port;
    Node *root;
    int socket;
    bool active;
    pthread_mutex_t lock;
    struct StorageServer *next; // For collision handling in storage server hash table
} StorageServer;

// Hash table for storage servers
typedef struct StorageServerTable
{
    StorageServer *table[TABLE_SIZE];
    pthread_mutex_t locks[TABLE_SIZE]; // Bucket-level locks for better concurrency
    int count;                         // Number of storage servers
} StorageServerTable;

typedef struct AcceptorArgs
{
    int server_fd;
    struct sockaddr_in server_addr;
    StorageServerTable *server_table;
} AcceptorArgs;

typedef struct NodeTable
{
    Node *table[TABLE_SIZE];
} NodeTable;

typedef struct StorageServerList
{
    StorageServer *server;
    struct StorageServerList *next;
} StorageServerList;

// typedef struct CacheNode
// {
//     Node *node;
//     char *key;
//     struct CacheNode *prev;
//     struct CacheNode *next;
// } CacheNode;

// typedef struct LRUCache
// {
//     int capacity;
//     int size;
//     CacheNode *head;
//     CacheNode *tail;
//     CacheNode **hashTable;
// } LRUCache;

// LRUCache *createLRUCache(int capacity);
// void freeLRUCache(LRUCache *cache);
// Node *getLRUCache(LRUCache *cache, const char *key);
// void putLRUCache(LRUCache *cache, const char *key, Node *node);
unsigned int hash(const char *str);
NodeTable *createNodeTable();
Node *createNode(const char *name, NodeType type, Permissions perms, const char *dataLocation);
void insertNode(NodeTable *table, Node *node);
Node *searchNode(NodeTable *table, const char *name);
void addFile(Node *parentDir, const char *fileName, Permissions perms, const char *dataLocation);
void addDirectory(Node *parentDir, const char *dirName, Permissions perms);
Node *searchPath(Node *root, const char *path);
void printFileSystemTree(Node *node, int depth);
char **splitPath(const char *path, int *count);
int hasPermission(Node *node, Permissions perm);
void listDirectory(Node *dir);

void freeNode(Node *node);
void traverseAndAdd(Node *parentDir, const char *path);
CommandType parseCommand(const char *cmd);
void printUsage();
void getParentPath(const char *path, char *parent);
void processCommand(Node *root);
Node *receiveNodeChain(int sock);
Node *createEmptyNode(Node *parentDir, const char *name, NodeType type);
int deleteNode(Node *node);
int copyNode(Node *sourceNode, Node *destDir, const char *newName);
ssize_t readFile(Node *fileNode, char *buffer, size_t size);
ssize_t writeFile(Node *fileNode, const char *buffer, size_t size);
int getFileMetadata(Node *fileNode, struct stat *metadata);
ssize_t streamAudioFile(Node *fileNode, char *buffer, size_t size, off_t offset);
int receiveServerInfo(int sock, char *ip_out, int *nm_port_out, int *client_port_out, Node **root_out);
StorageServerList *findStorageServersByPath_List(StorageServerTable *table, const char *path);
Node *findNode(Node *root, const char *path);
void recursiveList(Node *node, const char *current_path, char *response, int *response_offset, size_t response_size);
void copyDirectoryContents(Node *sourceDir, Node *destDir);
void *ackListener(void *arg);
void forwardAckToClient(const char *clientIP, int clientPort, const char *ack_message);
void logEvent(const char *level, const char *ip, int port, const char *message);
#endif
