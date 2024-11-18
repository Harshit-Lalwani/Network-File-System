#include"header.h"



ssize_t readFile(Node *fileNode, char *buffer, size_t size)
{
    if (fileNode->type != FILE_NODE)
    {
        printf("Error: Not a file\n");
        return -1;
    }

    if (!hasPermission(fileNode, READ))
    {
        printf("Error: No read permission\n");
        return -1;
    }

    int fd = open(fileNode->dataLocation, O_RDONLY);
    if (fd == -1)
    {
        perror("Error opening file");
        return -1;
    }

    ssize_t bytesRead = read(fd, buffer, size);
    if (bytesRead == -1)
    {
        perror("Error reading file");
    }

    close(fd);
    return bytesRead;
}

ssize_t writeFile(Node *fileNode, const char *buffer, size_t size)
{
    if (fileNode->type != FILE_NODE)
    {
        printf("Error: Not a file\n");
        return -1;
    }

    if (!hasPermission(fileNode, WRITE))
    {
        printf("Error: No write permission\n");
        return -1;
    }

    int flags = O_WRONLY;
    if (hasPermission(fileNode, APPEND))
    {
        flags |= O_APPEND;
    }
    else
    {
        flags |= O_TRUNC;
    }

    int fd = open(fileNode->dataLocation, flags);
    if (fd == -1)
    {
        perror("Error opening file");
        return -1;
    }

    ssize_t bytesWritten = write(fd, buffer, size);
    if (bytesWritten == -1)
    {
        perror("Error writing file");
    }

    close(fd);
    return bytesWritten;
}

int getFileMetadata(Node *fileNode, struct stat *metadata)
{
    if (!fileNode || !fileNode->dataLocation)
    {
        return -1;
    }

    return stat(fileNode->dataLocation, metadata);
}

ssize_t streamAudioFile(Node *fileNode, char *buffer, size_t size, off_t offset)
{
    if (fileNode->type != FILE_NODE)
    {
        printf("Error: Not a file\n");
        return -1;
    }

    if (!hasPermission(fileNode, READ))
    {
        printf("Error: No read permission\n");
        return -1;
    }

    int fd = open(fileNode->dataLocation, O_RDONLY);
    if (fd == -1)
    {
        perror("Error opening audio file");
        return -1;
    }

    // Seek to the specified offset
    if (lseek(fd, offset, SEEK_SET) == -1)
    {
        perror("Error seeking in file");
        close(fd);
        return -1;
    }

    ssize_t bytesRead = read(fd, buffer, size);
    if (bytesRead == -1)
    {
        perror("Error reading audio file");
    }

    close(fd);
    return bytesRead;
}

Node *createEmptyNode(Node *parentDir, const char *name, NodeType type)
{
    if (!parentDir || parentDir->type != DIRECTORY_NODE)
    {
        printf("Error: Parent is not a directory\n");
        return NULL;
    }

    // Check if node already exists
    if (searchNode(parentDir->children, name))
    {
        printf("Error: %s already exists\n", name);
        return NULL;
    }

    // Create the physical file/directory
    char fullPath[PATH_MAX];
    snprintf(fullPath, PATH_MAX, "%s/%s", parentDir->dataLocation, name);

    if (type == DIRECTORY_NODE)
    {
        if (mkdir(fullPath, 0755) != 0)
        {
            perror("Error creating directory");
            return NULL;
        }
    }
    else
    {
        int fd = open(fullPath, O_CREAT | O_WRONLY, 0644);
        if (fd == -1)
        {
            perror("Error creating file");
            return NULL;
        }
        close(fd);
    }

    // Create and insert the node
    Node *newNode = createNode(name, type, READ | WRITE, fullPath);
    newNode->parent = parentDir;
    insertNode(parentDir->children, newNode);
    return newNode;
}

int deleteNode(Node *node)
{
    if (!node || !node->parent)
    {
        printf("Error: Invalid node or root directory\n");
        return -1;
    }

    // Recursively delete all children if the node is a directory
    if (node->type == DIRECTORY_NODE && node->children)
    {
        for (int i = 0; i < TABLE_SIZE; i++)
        {
            Node *child = node->children->table[i];
            while (child)
            {
                Node *next = child->next;
                deleteNode(child);
                child = next;
            }
        }
    }

    // Remove the physical file or directory
    if (node->type == DIRECTORY_NODE)
    {
        if (rmdir(node->dataLocation) != 0)
        {
            perror("Error deleting directory");
            return -1;
        }
    }
    else
    {
        if (unlink(node->dataLocation) != 0)
        {
            perror("Error deleting file");
            return -1;
        }
    }

    // Remove node from parent's hash table
    unsigned int index = hash(node->name);
    Node *current = node->parent->children->table[index];
    Node *prev = NULL;

    while (current != NULL)
    {
        if (current == node)
        {
            if (prev == NULL)
            {
                node->parent->children->table[index] = current->next;
            }
            else
            {
                prev->next = current->next;
            }
            free(node->name);
            free(node->dataLocation);
            free(node);
            return 0;
        }
        prev = current;
        current = current->next;
    }

    return -1;
}

int copyNode(Node *sourceNode, Node *destDir, const char *newName)
{
    if (!sourceNode || !destDir || destDir->type != DIRECTORY_NODE)
    {
        printf("Error: Invalid source or destination\n");
        return -1;
    }

    // Create destination path
    char destPath[PATH_MAX];
    snprintf(destPath, PATH_MAX, "%s/%s",
             destDir->dataLocation,
             newName ? newName : sourceNode->name);

    if (sourceNode->type == DIRECTORY_NODE)
    {
        // Create new directory
        if (mkdir(destPath, 0755) != 0)
        {
            perror("Error creating destination directory");
            return -1;
        }

        // Create node in our file system
        Node *newDir = createNode(newName ? newName : sourceNode->name,
                                  DIRECTORY_NODE, sourceNode->permissions, destPath);
        newDir->parent = destDir;
        insertNode(destDir->children, newDir);

        // Copy contents recursively
        DIR *dir = opendir(sourceNode->dataLocation);
        if (!dir)
        {
            perror("Error opening source directory");
            return -1;
        }

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL)
        {
            if (strcmp(entry->d_name, ".") == 0 ||
                strcmp(entry->d_name, "..") == 0)
                continue;

            Node *childNode = searchNode(sourceNode->children, entry->d_name);
            if (childNode)
            {
                copyNode(childNode, newDir, NULL);
            }
        }
        closedir(dir);
    }
    else
    {
        // Copy file contents
        char buffer[8192];
        int sourceFd = open(sourceNode->dataLocation, O_RDONLY);
        int destFd = open(destPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);

        if (sourceFd == -1 || destFd == -1)
        {
            perror("Error opening files for copy");
            if (sourceFd != -1)
                close(sourceFd);
            if (destFd != -1)
                close(destFd);
            return -1;
        }

        ssize_t bytesRead;
        while ((bytesRead = read(sourceFd, buffer, sizeof(buffer))) > 0)
        {
            if (write(destFd, buffer, bytesRead) != bytesRead)
            {
                perror("Error writing to destination file");
                close(sourceFd);
                close(destFd);
                return -1;
            }
        }

        close(sourceFd);
        close(destFd);

        // Create node in our file system
        Node *newFile = createNode(newName ? newName : sourceNode->name,
                                   FILE_NODE, sourceNode->permissions, destPath);
        newFile->parent = destDir;
        insertNode(destDir->children, newFile);
    }

    return 0;
}