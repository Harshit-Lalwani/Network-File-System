#ifndef OPERATION_H
#define OPERATION_H
#include "heaher.h"

Node *createEmptyNode(Node *parentDir, const char *name, NodeType type);
int deleteNode(Node *node);
int copyNode(Node *sourceNode, Node *destDir, const char *newName);
ssize_t readFile(Node *fileNode, char *buffer, size_t size);
ssize_t writeFile(Node *fileNode, const char *buffer, size_t size);
int getFileMetadata(Node *fileNode, struct stat *metadata);
ssize_t streamAudioFile(Node *fileNode, char *buffer, size_t size, off_t offset);
#endif