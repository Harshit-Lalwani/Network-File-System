#include"header.h"

// Hash function for strings
unsigned int hash(const char *str)
{
    unsigned int hash = 0;
    while (*str)
    {
        hash = (hash * 31) + *str;
        str++;
    }
    return hash % TABLE_SIZE;
}

// Initialize a new hash table for storing children
NodeTable *createNodeTable()
{
    NodeTable *nodeTable = (NodeTable *)malloc(sizeof(NodeTable));
    for (int i = 0; i < TABLE_SIZE; i++)
        nodeTable->table[i] = NULL;
    return nodeTable;
}

// Helper to create a new node (file or directory) with metadata
Node *createNode(const char *name, NodeType type, Permissions perms, const char *dataLocation)
{
    Node *node = (Node *)malloc(sizeof(Node));
    node->name = strdup(name);
    node->type = type;
    node->permissions = perms;
    node->dataLocation = dataLocation ? strdup(dataLocation) : NULL;
    node->parent = NULL;
    node->next = NULL;
    node->lock_type=0;
    node->children = (type == DIRECTORY_NODE) ? createNodeTable() : NULL;
    node->lock_type = 0; // No lock by default
    return node;
}

// Insert a node into a directory's hash table
void insertNode(NodeTable *table, Node *node)
{
    unsigned int index = hash(node->name);
    node->next = table->table[index];
    table->table[index] = node;
}

// Search for a file or directory in a hash table by name
Node *searchNode(NodeTable *table, const char *name)
{
    unsigned int index = hash(name);
    Node *current = table->table[index];
    while (current && strcmp(current->name, name) != 0)
    {
        current = current->next;
    }
    return current;
}

// Add a file under a directory with metadata
void addFile(Node *parentDir, const char *fileName, Permissions perms, const char *dataLocation)
{
    if (parentDir->type != DIRECTORY_NODE)
    {
        printf("Cannot add file to a non-directory node\n");
        return;
    }

    Node *newFile = createNode(fileName, FILE_NODE, perms, dataLocation);
    newFile->parent = parentDir;
    insertNode(parentDir->children, newFile);
}

// Add a directory under a directory
void addDirectory(Node *parentDir, const char *dirName, Permissions perms)
{
    if (parentDir->type != DIRECTORY_NODE)
    {
        printf("Cannot add directory to a non-directory node\n");
        return;
    }

    Node *newDir = createNode(dirName, DIRECTORY_NODE, perms, NULL);
    newDir->parent = parentDir;
    insertNode(parentDir->children, newDir);
}

// Recursive function to search for a file or directory by path
void printFileSystemTree(Node *node, int depth)
{
    if (!node)
        return;

    // Print indentation
    for (int i = 0; i < depth; i++)
    {
        printf("  ");
    }

    // Print current node
    printf("|-- %s (%s)\n", node->name,
           node->type == FILE_NODE ? "File" : "Directory");

    // If it's a directory, print all its children
    if (node->type == DIRECTORY_NODE && node->children)
    {
        for (int i = 0; i < TABLE_SIZE; i++)
        {
            Node *current = node->children->table[i];
            while (current)
            {
                printFileSystemTree(current, depth + 1);
                current = current->next;
            }
        }
    }
}

// Helper function to split path into components
char **splitPath(const char *path, int *count)
{
    char *pathCopy = strdup(path);
    char *temp = pathCopy;
    *count = 0;

    // Count components
    while (*temp)
    {
        if (*temp == '/')
            (*count)++;
        temp++;
    }
    (*count)++; // For the last component

    // Allocate array for components
    char **components = malloc(sizeof(char *) * (*count));
    int idx = 0;

    // Split path
    char *token = strtok(pathCopy, "/");
    while (token && idx < *count)
    {
        components[idx++] = strdup(token);
        token = strtok(NULL, "/");
    }
    *count = idx; // Update actual count

    free(pathCopy);
    return components;
}

// Modified search path function
Node *searchPath(Node *root, const char *path)
{
    // printf("\nSearching for path: %s\n", path);

    // Split path into components
    int componentCount;
    char **pathComponents = splitPath(path, &componentCount);

    Node *current = root;

    // Skip the first component if it matches the root name
    int startIdx = 0;
    if (componentCount > 0 && strcmp(pathComponents[0], root->name) == 0)
    {
        startIdx = 1;
    }

    // Traverse path components
    for (int i = startIdx; i < componentCount && current != NULL; i++)
    {
        // printf("Looking for component: %s in directory: %s\n",
        //        pathComponents[i], current->name);

        if (current->type != DIRECTORY_NODE)
        {
            printf("Error: %s is not a directory\n", current->name);
            current = NULL;
            break;
        }

        // Search in current directory's hash table
        Node *found = searchNode(current->children, pathComponents[i]);

        if (!found)
        {
            // printf("Component not found: %s\n", pathComponents[i]);
            // // Print contents of current directory for debugging
            // printf("Contents of directory %s:\n", current->name);
            // for (int j = 0; j < TABLE_SIZE; j++)
            // {
            //     Node *child = current->children->table[j];
            //     while (child)
            //     {
            //         printf("  - %s\n", child->name);
            //         child = child->next;
            //     }
            // }
            current = NULL;
            break;
        }

        current = found;
        // printf("Found component: %s\n", current->name);
    }

    // Free path components
    for (int i = 0; i < componentCount; i++)
    {
        free(pathComponents[i]);
    }
    free(pathComponents);

    return current;
}

// Check if a node has specific permissions
int hasPermission(Node *node, Permissions perm)
{
    return (node->permissions & perm) != 0;
}

// Display directory contents (metadata only)
void listDirectory(Node *dir)
{
    if (dir->type != DIRECTORY_NODE)
    {
        printf("%s is not a directory\n", dir->name);
        return;
    }

    printf("Contents of directory %s:\n", dir->name);
    for (int i = 0; i < TABLE_SIZE; i++)
    {
        Node *child = dir->children->table[i];
        while (child)
        {
            printf("- %s (%s), Location: %s, Permissions: %d\n",
                   child->name,
                   child->type == FILE_NODE ? "File" : "Directory",
                   child->dataLocation ? child->dataLocation : "N/A",
                   child->permissions);
            child = child->next;
        }
    }
}

// Cleanup function to free allocated memory
void freeNode(Node *node)
{
    if (node->children)
    {
        for (int i = 0; i < TABLE_SIZE; i++)
        {
            Node *child = node->children->table[i];
            while (child)
            {
                Node *next = child->next;
                freeNode(child);
                child = next;
            }
        }
        free(node->children);
    }
    free(node->name);
    if (node->dataLocation)
        free(node->dataLocation);
    free(node);
}

// Traverse the file system starting from `path` and add all files/directories to `parentDir`
void traverseAndAdd(Node *parentDir, const char *path)
{
    DIR *dir = opendir(path);
    if (!dir)
    {
        perror("opendir failed");
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 || (entry->d_name[0] == '.'))
            continue;

        char fullPath[1024];
        if (snprintf(fullPath, sizeof(fullPath), "%s/%s", path, entry->d_name) >= sizeof(fullPath))
        {
            fprintf(stderr, "Path too long: %s/%s\n", path, entry->d_name);
            continue;
        }

        struct stat st;
        if (stat(fullPath, &st) == -1)
        {
            fprintf(stderr, "Failed to stat %s: %s\n", fullPath, strerror(errno));
            continue;
        }

        NodeType type = (S_ISDIR(st.st_mode)) ? DIRECTORY_NODE : FILE_NODE;
        Permissions perms = 0;

        if (st.st_mode & S_IRUSR)
            perms |= READ;
        if (st.st_mode & S_IWUSR)
            perms |= WRITE;
        if (st.st_mode & S_IXUSR)
            perms |= EXECUTE;

        Node *newNode = createNode(entry->d_name, type, perms, fullPath);
        newNode->parent = parentDir;

        // printf("Inserting: %s (type: %s)\n", entry->d_name, (type == DIRECTORY_NODE) ? "Directory" : "File");
        // printf("Full path: %s\n", fullPath); // Debug print to check the full path

        insertNode(parentDir->children, newNode);

        if (type == DIRECTORY_NODE)
        {
            traverseAndAdd(newNode, fullPath); // Recursively traverse subdirectories
        }
    }
    closedir(dir);
}
