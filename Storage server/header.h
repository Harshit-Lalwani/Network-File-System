#ifndef HEADER_H
#define HEADER_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <linux/limits.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <asm-generic/socket.h>
#define TABLE_SIZE 10
#define MAX_COMMAND_LENGTH 10
#define MAX_PATH_LENGTH 1024
#define MAX_CONTENT_LENGTH 100001
#define CHUNK_SIZE 100001
#define BUFFER_SIZE 100001
#define MAX_BUFFER_SIZE 100001
#define ACK_PORT 8090

typedef enum
{
    CMD_READ,
    CMD_WRITE,
    CMD_META,
    CMD_STREAM,
    CMD_CREATE,
    CMD_DELETE,
    CMD_COPY,
    CMD_FILECOPY,
    CMD_DIRCOPY,
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

struct ClientData
{
    int socket;
    Node *root;
};

typedef struct
{
    Node *root;
    char command[MAX_COMMAND_LENGTH];
    int socket;
} ThreadArgs;

typedef struct NodeTable
{
    Node *table[TABLE_SIZE];
} NodeTable;

typedef struct AsyncWriteTask {
    Node *targetNode;
    char *data;
    size_t size;
    int clientId; // To identify the client socket
    char clientIP[INET_ADDRSTRLEN]; // Store client IP
    int clientPort; // Store client port
    int writeStatus;
    struct AsyncWriteTask *next;
} AsyncWriteTask;

extern AsyncWriteTask *asyncWriteQueue; // The head of the queue
extern pthread_mutex_t queueMutex;      // Mutex for queue protection
extern pthread_cond_t queueCondition;   // Condition variable for signaling

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
void processCommand_namingServer(Node *root, char *input, int client_socket);
void processCommand_user(Node *root, char *input, int client_socket);
Node *createEmptyNode(Node *parentDir, const char *name, NodeType type);
int deleteNode(Node *node);
int copyNode(Node *sourceNode, Node *destDir, const char *newName);
int getFileMetadata(Node *fileNode, struct stat *metadata);
ssize_t streamAudioFile(Node *fileNode, char *buffer, size_t size, off_t offset);
int copy_directory_recursive(int peer_socket, Node *dir_node, const char *dest_path, int naming_socket);
void copy_files_to_peer(const char *source_path, const char *dest_path, const char *peer_ip, int peer_port, Node *root, int naming_socket);
int copy_single_file(int peer_socket, Node *source_node, const char *dest_path, int naming_socket);
Node *findNode(Node *root, const char *path);
void *flushAsyncWrites();
#endif
