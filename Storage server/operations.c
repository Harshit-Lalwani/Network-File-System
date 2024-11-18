#include "header.h"

// ssize_t readFile(Node *fileNode, char *buffer, size_t size)
// {
//     if (fileNode->type != FILE_NODE)
//     {
//         printf("Error: Not a file\n");
//         return -1;
//     }

//     if (!hasPermission(fileNode, READ))
//     {
//         printf("Error: No read permission\n");
//         return -1;
//     }

//     int fd = open(fileNode->dataLocation, O_RDONLY);
//     if (fd == -1)
//     {
//         perror("Error opening file");
//         return -1;
//     }

//     ssize_t bytesRead = read(fd, buffer, size);
//     if (bytesRead == -1)
//     {
//         perror("Error reading file");
//     }

//     close(fd);
//     return bytesRead;
// }

// ssize_t writeFile(Node *fileNode, const char *buffer, size_t size)
// {
//     if (fileNode->type != FILE_NODE)
//     {
//         printf("Error: Not a file\n");
//         return -1;
//     }

//     if (!hasPermission(fileNode, WRITE))
//     {
//         printf("Error: No write permission\n");
//         return -1;
//     }

//     int flags = O_WRONLY;
//     if (hasPermission(fileNode, APPEND))
//     {
//         flags |= O_APPEND;
//     }
//     else
//     {
//         flags |= O_TRUNC;
//     }

//     int fd = open(fileNode->dataLocation, flags);
//     if (fd == -1)
//     {
//         perror("Error opening file");
//         return -1;
//     }

//     ssize_t bytesWritten = write(fd, buffer, size);
//     if (bytesWritten == -1)
//     {
//         perror("Error writing file");
//     }

//     close(fd);
//     return bytesWritten;
// }

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

int connectToServer(const char *ip, int port)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        perror("Socket creation failed");
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0)
    {
        perror("Invalid address");
        close(sock);
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Connection failed");
        close(sock);
        return -1;
    }

    return sock;
}
Node *findNode(Node *root, const char *path)
{
    if (!root || !path || strlen(path) == 0)
    {
        printf("Error: Invalid root or path.\n");
        return NULL;
    }

    // Handle root path case
    if (strcmp(path, "/") == 0)
    {
        return root;
    }

    // Tokenize the path using the path separator
    char *pathCopy = strdup(path); // Make a mutable copy of the path
    char *token = strtok(pathCopy, "/");
    Node *current = root;

    while (token != NULL)
    {
        // Traverse the children of the current node
        NodeTable *childrenTable = current->children;
        if (!childrenTable)
        {
            printf("Error: Path component '%s' not found (no children).\n", token);
            free(pathCopy);
            return NULL;
        }

        Node *child = NULL;
        for (int i = 0; i < TABLE_SIZE; i++)
        {
            child = childrenTable->table[i];
            while (child != NULL)
            {
                if (strcmp(child->name, token) == 0)
                {
                    break;
                }
                child = child->next;
            }
            if (child)
            {
                break;
            }
        }

        if (!child)
        {
            printf("Error: Path component '%s' not found.\n", token);
            free(pathCopy);
            return NULL;
        }

        // Move to the next level
        current = child;
        token = strtok(NULL, "/");
    }

    free(pathCopy);
    return current;
}

void copy_files_to_peer(const char *source_path, const char *dest_path, const char *peer_ip, int peer_port, Node *root, int naming_socket)
{
    Node *source_node = findNode(root, source_path);
    int peer_socket = connectToServer(peer_ip, peer_port);
    if (peer_socket < 0)
        return;

    if (source_node->type == FILE_NODE)
    {
        copy_single_file(peer_socket, source_node, dest_path, naming_socket);
    }
    else if (source_node->type == DIRECTORY_NODE)
    {
        copy_directory_recursive(peer_socket, source_node, dest_path);
    }

    close(peer_socket);
}

void copy_single_file(int peer_socket, Node *source_node, const char *dest_path, int naming_socket)
{
    // Send file metadata
    char metadata[MAX_BUFFER_SIZE];
    snprintf(metadata, sizeof(metadata), "FILE_META %s %s %d", dest_path, source_node->name, source_node->permissions);
    send(peer_socket, metadata, strlen(metadata), 0);
    char respond[1024];
    recv(peer_socket, respond, sizeof(respond), 0);
    printf("%s\n", respond);
    if (strncmp(respond, "CREATE DONE", 11) == 0)
    {
        FILE *fp = fopen(source_node->dataLocation, "rb");
        if (!fp)
            return;

        char buffer[100001];
        size_t bytes_read;
        char com[20];
        while ((bytes_read = fread(buffer, 1, sizeof(buffer), fp)) > 0)
        {
            printf("%s\n", buffer);
            send(peer_socket, buffer, bytes_read, 0);
            recv(peer_socket, com, sizeof(com), 0);
            memset(buffer, 0, sizeof(buffer));
        }
        fclose(fp);
        send(peer_socket, "END_OF_FILE\n", strlen("END_OF_FILE\n"), 0);
        recv(peer_socket, buffer, sizeof(buffer), 0);
        send(naming_socket, "FILE COPY DONE", strlen("FILE COPY DONE"), 0);
    }
    else
    {
        send(naming_socket, respond, strlen(respond), 0);
    }
}

void copy_directory_recursive(int peer_socket, Node *dir_node, const char *dest_path)
{
    // Create directory on peer
    char dir_cmd[MAX_BUFFER_SIZE];
    snprintf(dir_cmd, sizeof(dir_cmd), "CREATE_DIR %s %s %d",
             dest_path, dir_node->name, dir_node->permissions);
    send(peer_socket, dir_cmd, strlen(dir_cmd), 0);

    // Recursively copy all children
    for (int i = 0; i < TABLE_SIZE; i++)
    {
        Node *child = dir_node->children->table[i];
        while (child)
        {
            char new_dest_path[MAX_PATH_LENGTH];
            snprintf(new_dest_path, sizeof(new_dest_path), "%s/%s",
                     dest_path, dir_node->name);

            if (child->type == FILE_NODE)
            {
                // copy_single_file(peer_socket, child, new_dest_path);
            }
            else
            {
                copy_directory_recursive(peer_socket, child, new_dest_path);
            }
            child = child->next;
        }
    }
}

// Peer storage server receiving side
void handle_peer_copy(int peer_socket)
{
    char buffer[MAX_BUFFER_SIZE];
    while (1)
    {
        ssize_t bytes_received = recv(peer_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0)
            break;
        buffer[bytes_received] = '\0';

        if (strncmp(buffer, "FILE_META", 9) == 0)
        {
            char path[MAX_PATH_LENGTH], filename[MAX_PATH_LENGTH];
            int perms;
            sscanf(buffer, "FILE_META %s %s %d", path, filename, &perms);

            // Create parent directories if needed
            // Node *parent = ensure_parent_directories(root, path);
            // if (!parent)
            continue;

            // Create new file node and receive content
            char file_path[MAX_PATH_LENGTH];
            // snprintf(file_path, sizeof(file_path), "%s/%s", DATA_DIR, filename);
            // addFile(parent, filename, perms, file_path);

            // Receive and write file content
            receive_file_content(peer_socket, file_path);
        }
        else if (strncmp(buffer, "CREATE_DIR", 10) == 0)
        {
            char path[MAX_PATH_LENGTH], dirname[MAX_PATH_LENGTH];
            int perms;
            sscanf(buffer, "CREATE_DIR %s %s %d", path, dirname, &perms);

            // Node *parent = ensure_parent_directories(root, path);
            // if (parent)
            // {
            //     addDirectory(parent, dirname, perms);
            // }
        }
    }
}

// Helper function to receive file content
void receive_file_content(int peer_socket, const char *file_path)
{
    FILE *fp = fopen(file_path, "wb");
    if (!fp)
        return;

    char buffer[8192];
    ssize_t bytes_received;
    while ((bytes_received = recv(peer_socket, buffer, sizeof(buffer), 0)) > 0)
    {
        fwrite(buffer, 1, bytes_received, fp);
    }
    fclose(fp);
}