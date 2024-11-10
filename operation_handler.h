#ifndef OPERATION_HANDLER_H
#define OPERATION_HANDLER_H
#include"heaher.h"
CommandType parseCommand(const char *cmd);
void printUsage();
void processCommand(Node *root);
#endif