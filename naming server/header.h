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
#define TABLE_SIZE 10
#define MAX_COMMAND_LENGTH 10
#define MAX_PATH_LENGTH 1024
#define MAX_CONTENT_LENGTH 4096
#define CHUNK_SIZE 1024

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
} Node;

typedef struct NodeTable
{
    Node *table[TABLE_SIZE];
} NodeTable;

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
void processCommand(Node *root);
Node *createEmptyNode(Node *parentDir, const char *name, NodeType type);
int deleteNode(Node *node);
int copyNode(Node *sourceNode, Node *destDir, const char *newName);
ssize_t readFile(Node *fileNode, char *buffer, size_t size);
ssize_t writeFile(Node *fileNode, const char *buffer, size_t size);
int getFileMetadata(Node *fileNode, struct stat *metadata);
ssize_t streamAudioFile(Node *fileNode, char *buffer, size_t size, off_t offset);

#endif
