#include"header.h"

int main()
{
    // Create root directory
    Node *root = createNode("home", DIRECTORY_NODE, READ | WRITE | EXECUTE, "/home");

    // Start traversal from the root directory
    traverseAndAdd(root, "/home");

    printf("\nFile System initialized. Type a command to begin.\n");
    printUsage();

    // Start processing commands
    processCommand(root);

    // Cleanup
    freeNode(root);
    return 0;
}
