#include "header.h"

CommandType parseCommand(const char *cmd)
{
    if (strcasecmp(cmd, "READ") == 0)
        return CMD_READ;
    if (strcasecmp(cmd, "WRITE") == 0)
        return CMD_WRITE;
    if (strcasecmp(cmd, "META") == 0)
        return CMD_META;
    if (strcasecmp(cmd, "STREAM") == 0)
        return CMD_STREAM;
    if (strcasecmp(cmd, "CREATE") == 0)
        return CMD_CREATE;
    if (strcasecmp(cmd, "DELETE") == 0)
        return CMD_DELETE;
    if (strcasecmp(cmd, "COPY") == 0)
    {
        // printf("heuiewjdn\n");
        return CMD_COPY;
    }
    if (strcasecmp(cmd, "FILE_META") == 0)
        return CMD_FILECOPY;
    if (strcasecmp(cmd, "CREATE_DIR") == 0)
        return CMD_DIRCOPY;
    return CMD_UNKNOWN;
}

// Update the usage information
void printUsage()
{
    printf("\nAvailable commands:\n");
    printf("READ <path>                    - Read contents of a file\n");
    printf("WRITE <path>                   - Write content to a file\n");
    printf("META <path>                    - Get file metadata\n");
    printf("STREAM <path>                  - Stream an audio file\n");
    printf("CREATE FILE <path>             - Create an empty file\n");
    printf("CREATE DIR <path>              - Create an empty directory\n");
    printf("DELETE <path>                  - Delete a file or directory\n");
    printf("COPY <source> <destination>    - Copy file or directory\n");
    printf("EXIT                           - Exit the program\n");
}

ssize_t readFileChunk(Node *node, char *buffer, size_t size, off_t offset)
{
    int fd = open(node->dataLocation, O_RDONLY);
    if (fd < 0)
        return -1;

    lseek(fd, offset, SEEK_SET);
    ssize_t bytes = read(fd, buffer, size);
    close(fd);

    return bytes;
}

// Helper function to write file in chunks
ssize_t writeFileChunk(Node *node, const char *buffer, size_t size, off_t offset)
{
    int fd = open(node->dataLocation, O_WRONLY);
    if (fd < 0)
        return -1;

    lseek(fd, offset, SEEK_SET);
    ssize_t bytes = write(fd, buffer, size);
    close(fd);

    return bytes;
}

void getPermissionsString(int mode, char *permissions, size_t size)
{
    permissions[0] = '\0'; // Start with an empty string
    if (mode & READ)
        strncat(permissions, "READ ", size - strlen(permissions) - 1);
    if (mode & WRITE)
        strncat(permissions, "WRITE ", size - strlen(permissions) - 1);
    if (mode & EXECUTE)
        strncat(permissions, "EXECUTE ", size - strlen(permissions) - 1);
    if (mode & APPEND)
        strncat(permissions, "APPEND ", size - strlen(permissions) - 1);
}

int min(int a, int b)
{
    if (a < b)
        return a;
    return b;
}

void processCommand_user(Node *root, char *input, int client_socket)
{
    char path[MAX_PATH_LENGTH];
    char buffer[100001];
    char secondPath[MAX_PATH_LENGTH];
    char typeStr[5];
    struct stat metadata;
    char command[20];
    char response[1024];

    // Clear any leading/trailing whitespace
    char *cmd_start = input;
    while (*cmd_start == ' ')
        cmd_start++;
    if (strlen(cmd_start) == 0)
    {
        send(client_socket, "Error: Empty command\n", strlen("Error: Empty command\n"), 0);
        return;
    }
    if (sscanf(cmd_start, "%s", command) != 1)
    {
        send(client_socket, "Error reading command\n", strlen("Error reading command\n"), 0);
        return;
    }
    cmd_start += strlen(command);
    while (*cmd_start == ' ')
        cmd_start++;

    if (strcmp(command, "EXIT") == 0)
    {
        send(client_socket, "Exiting...\n", strlen("Exiting...\n"), 0);
        return;
    }

    CommandType cmd = parseCommand(command);
    switch (cmd)
    {
    case CMD_READ:
    case CMD_WRITE:
    case CMD_META:
    case CMD_STREAM:
        if (sscanf(cmd_start, "%s", path) != 1)
        {
            send(client_socket, "Error: Path required\n", strlen("Error: Path required\n"), 0);
            return;
        }

        Node *targetNode = searchPath(root, path);
        if (!targetNode)
        {
            memset(response, 0, sizeof(response));
            snprintf(response, sizeof(response), "Path not found\n");
            send(client_socket, response, strlen(response), 0);
            return;
        }

        if (cmd == CMD_READ)
        {
            ssize_t bytes;
            off_t offset = 0;
            struct stat st;
            if (getFileMetadata(targetNode, &st) == 0)
            {
                memset(response, 0, sizeof(response));
                snprintf(response, sizeof(response), "FILE_SIZE:%ld\n", st.st_size);
                send(client_socket, response, strlen(response), 0);
                memset(buffer, 0, sizeof(buffer));
                recv(client_socket, buffer, sizeof(buffer), 0);
            }
            memset(buffer, 0, sizeof(buffer));

            while ((bytes = readFileChunk(targetNode, buffer, sizeof(buffer), offset)) > 0)
            {
                // printf("%s\n", buffer);
                send(client_socket, buffer, bytes, 0);
                memset(buffer, 0, sizeof(buffer));
                recv(client_socket, buffer, sizeof(buffer), 0);
                offset += bytes;
                memset(buffer, 0, sizeof(buffer));
            }

            // Send end marker
            send(client_socket, "END_OF_FILE\n", strlen("END_OF_FILE\n"), 0);
            memset(buffer, 0, sizeof(buffer));
            recv(client_socket, buffer, sizeof(buffer), 0);
        }
        else if (cmd == CMD_WRITE)
        {
            send(client_socket, "Error: Invalid file size format\n", strlen("Error: Invalid file size format\n"), 0);

            // First receive file size from client
            memset(buffer, 0, sizeof(buffer));
            recv(client_socket, buffer, sizeof(buffer), 0);

            long fileSize;
            if (sscanf(buffer, "FILE_SIZE:%ld", &fileSize) != 1)
            {
                send(client_socket, "Error: Invalid file size format\n",
                     strlen("Error: Invalid file size format\n"), 0);
                return;
            }

            // Send acknowledgment
            send(client_socket, "READY_TO_RECEIVE\n", strlen("READY_TO_RECEIVE\n"), 0);

            // Receive file content in chunks
            long totalReceived = 0;
            while (totalReceived < fileSize)
            {
                memset(buffer, 0, sizeof(buffer));
                ssize_t bytesReceived = recv(client_socket, buffer,
                                             min(sizeof(buffer), fileSize - totalReceived), 0);

                if (bytesReceived <= 0)
                {
                    send(client_socket, "Error receiving file data\n",
                         strlen("Error receiving file data\n"), 0);
                    return;
                }

                if (writeFileChunk(targetNode, buffer, bytesReceived, totalReceived) != bytesReceived)
                {
                    send(client_socket, "Error writing to file\n",
                         strlen("Error writing to file\n"), 0);
                    return;
                }
                totalReceived += bytesReceived;
            }

            memset(response, 0 , sizeof(response));
            snprintf(response, sizeof(response), "Successfully wrote %ld bytes\n", totalReceived);
            send(client_socket, response, strlen(response), 0);
        }
        else if (cmd == CMD_META)
        {
            if (getFileMetadata(targetNode, &metadata) == 0)
            {
                char permissions[64];
                getPermissionsString(metadata.st_mode & 0777, permissions, sizeof(permissions));
                memset(response, 0 , sizeof(response));
                snprintf(response, sizeof(response),
                         "File Metadata:\nName: %s\nType: %s\nSize: %ld bytes\n"
                         "Permissions: %s\nLast access: %sLast modification: %s\n",
                         targetNode->name,
                         targetNode->type == FILE_NODE ? "File" : "Directory",
                         metadata.st_size,
                         permissions,
                         ctime(&metadata.st_atime),
                         ctime(&metadata.st_mtime));
                send(client_socket, response, strlen(response), 0);
            }
            else
            {
                send(client_socket, "Error getting metadata\n",
                     strlen("Error getting metadata\n"), 0);
            }
        }
        else if (cmd == CMD_STREAM)
        {
            off_t offset = 0;
            int chunks = 0;
            ssize_t bytes;

            send(client_socket, "START_STREAM\n", strlen("START_STREAM\n"), 0);

            while ((bytes = streamAudioFile(targetNode, buffer, CHUNK_SIZE, offset)) > 0)
            {
                if (send(client_socket, buffer, bytes, 0) != bytes)
                {
                    send(client_socket, "Error streaming data\n",
                         strlen("Error streaming data\n"), 0);
                    break;
                }
                memset(buffer, 0, sizeof(buffer));
                recv(client_socket, buffer, sizeof(buffer), 0);
                memset(buffer, 0, sizeof(buffer));
                offset += bytes;
                chunks++;
                usleep(100000);
            }
            send(client_socket, "END_STREAM\n", strlen("END_STREAM\n"), 0);
            memset(buffer, 0, sizeof(buffer));
            recv(client_socket, buffer, sizeof(buffer), 0);
        }
        break;
    case CMD_FILECOPY:
        char name[1024];
        int permissions;
        if (sscanf(cmd_start, "%s %s %d", path, name, &permissions) != 3)
        {
            send(client_socket, "Error: Path required\n", strlen("Error: Path required\n"), 0);
            return;
        }
        Node *parentDir = findNode(root, path);
        if (!parentDir)
        {
            memset(response, 0 , sizeof(response));
            snprintf(response, sizeof(response), "Error: Parent directory not found");
            send(client_socket, response, strlen(response), 0);
            memset(response, 0, sizeof(response));
            return;
        }

        NodeType type = FILE_NODE;
        if (createEmptyNode(parentDir, name, type))
        {
            memset(response, 0 , sizeof(response));
            snprintf(response, sizeof(response), "CREATE DONE");
            send(client_socket, response, strlen(response), 0);
            memset(response, 0, sizeof(response));
            memset(buffer, 0, sizeof(buffer));
            size_t bytes_received;
            int totalReceived = 0;
            char node_path[1024];
            snprintf(node_path, sizeof(node_path), "%s/%s", path, name);
            // printf("%s\n", node_path);
            Node *target = findNode(root, node_path);
            memset(buffer, 0, sizeof(buffer));
            while ((bytes_received = recv(client_socket, buffer, sizeof(buffer), 0)) > 0)
            {
                send(client_socket, command, strlen(command), 0);
                // printf("%d\n",bytes_received);
                // size+=bytes_received;
                buffer[bytes_received] = '\0';
                if (strncmp(buffer, "END_OF_FILE\n", 12) == 0)
                    break;
                writeFileChunk(target, buffer, bytes_received, totalReceived);
                memset(buffer, 0, sizeof(buffer));
            }
            // return;
        }
        else
        {
            memset(response, 0 , sizeof(response));
            snprintf(response, sizeof(response), "Error creating node");
            send(client_socket, response, strlen(response), 0);
            memset(response, 0, sizeof(response));
            return;
        }
        break;
    case CMD_DIRCOPY:
        char name2[1024];
        int permissions2;
        if (sscanf(cmd_start, "%s %s %d", path, name2, &permissions2) != 3)
        {
            send(client_socket, "Error: Path required\n", strlen("Error: Path required\n"), 0);
            return;
        }
        parentDir = findNode(root, path);
        if (!parentDir)
        {
            memset(response, 0 , sizeof(response));
            snprintf(response, sizeof(response), "Error: Parent directory not found");
            send(client_socket, response, strlen(response), 0);
            memset(response, 0, sizeof(response));
            return;
        }

        type = DIRECTORY_NODE;
        if (createEmptyNode(parentDir, name2, type))
        {
            memset(response, 0 , sizeof(response));
            snprintf(response, sizeof(response), "CREATE DONE");
            send(client_socket, response, strlen(response), 0);
            memset(response, 0, sizeof(response));
            return;
        }
        else
        {
            memset(response, 0 , sizeof(response));
            snprintf(response, sizeof(response), "CREATE FAILED");
            send(client_socket, response, strlen(response), 0);
            memset(response, 0, sizeof(response));
            return;
        }
        break;

    case CMD_UNKNOWN:
        send(client_socket, "Unknown command: %s\nUsage: READ|WRITE|META|STREAM <args>\n", strlen("Unknown command: %s\nUsage: READ|WRITE|META|STREAM <args>\n"), 0);
        break;
    }
}

void processCommand_namingServer(Node *root, char *input, int client_socket)
{
    char path[MAX_PATH_LENGTH];
    char buffer[MAX_CONTENT_LENGTH];
    char secondPath[MAX_PATH_LENGTH];
    char typeStr[5];
    struct stat metadata;
    char command[20];
    char response[100001];
    printf("helllo\n");
    char *cmd_start = input;
    while (*cmd_start == ' ')
        cmd_start++;
    if (strlen(cmd_start) == 0)
    {
        printf("hey\n");
        send(client_socket, "Error: Empty command\n", strlen("Error: Empty command\n"), 0);
        return;
    }
    if (sscanf(cmd_start, "%s", command) != 1)
    {
        send(client_socket, "Error reading command\n", strlen("Error reading command\n"), 0);
        return;
    }
    cmd_start += strlen(command);
    while (*cmd_start == ' ')
        cmd_start++;

    if (strcmp(command, "EXIT") == 0)
    {
        send(client_socket, "Exiting...\n", strlen("Exiting...\n"), 0);
        return;
    }
    printf("%s\n", command);
    CommandType cmd = parseCommand(command);
    // printf("%d\n",cmd);
    int temp;
    switch (cmd)
    {
    case CMD_CREATE:
        if (sscanf(cmd_start, "%s %d %s", typeStr, &temp, path) != 3)
        {
            memset(response, 0 , sizeof(response));
            snprintf(response, sizeof(response), "Error: Type and path required");
            send(client_socket, response, strlen(response), 0);
            memset(response, 0, sizeof(response));
            return;
        }
        char *lastSlash = strrchr(path, '/');
        if (!lastSlash)
        {
            memset(response, 0 , sizeof(response));
            snprintf(response, sizeof(response), "Error: Invalid path format");
            send(client_socket, response, strlen(response), 0);
            memset(response, 0, sizeof(response));
            return;
        }
        *lastSlash = '\0';
        char *name = lastSlash + 1;
        Node *parentDir = searchPath(root, path);
        *lastSlash = '/';

        if (!parentDir)
        {
            memset(response, 0 , sizeof(response));
            snprintf(response, sizeof(response), "Error: Parent directory not found");
            send(client_socket, response, strlen(response), 0);
            memset(response, 0, sizeof(response));
            return;
        }

        NodeType type = (strcasecmp(typeStr, "DIR") == 0) ? DIRECTORY_NODE : FILE_NODE;
        if (createEmptyNode(parentDir, name, type))
        {
            memset(response, 0 , sizeof(response));
            snprintf(response, sizeof(response), "CREATE DONE");
            send(client_socket, response, strlen(response), 0);
            memset(response, 0, sizeof(response));
            return;
        }
        else
        {
            memset(response, 0 , sizeof(response));
            snprintf(response, sizeof(response), "Error creating node");
            send(client_socket, response, strlen(response), 0);
            memset(response, 0, sizeof(response));
        }
        break;

    case CMD_COPY:
        sscanf(cmd_start, "%s %s", path, secondPath);
        memset(buffer, 0 , sizeof(buffer));
        snprintf(buffer, sizeof(buffer), "ACknowledgement");
        ssize_t bytes_sent = send(client_socket, buffer, strlen(buffer), 0);
        printf("Bytes sent: %zd\n", bytes_sent);
        fflush(stdout);

        if (bytes_sent <= 0)
        {
            perror("Failed to send COPY command to source");
            fflush(stdout);
            return;
        }
        printf("sent %s\n", buffer);
        fflush(stdout);
        memset(buffer, 0 , sizeof(buffer));
        ssize_t bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0)
            break;
        buffer[bytes_received] = '\0';
        char peer_ip[16];
        int peer_port;
        int flag = 0;
        if (strncmp(buffer, "SOURCE SERVER_INFO", 18) == 0)
        {
            printf("source\n");
            sscanf(buffer, "SOURCE SERVER_INFO %s %d", peer_ip, &peer_port);
            copy_files_to_peer(path, secondPath, peer_ip, peer_port, root, client_socket);
            flag = 1;
        }
        break;

    case CMD_DELETE:
        if (sscanf(cmd_start, "%s", path) != 1)
        {
            memset(response, 0 , sizeof(response));
            snprintf(response, sizeof(response), "Error: Path required");
            send(client_socket, response, strlen(response), 0);
            memset(response, 0, sizeof(response));
            return;
        }
        Node *nodeToDelete = searchPath(root, path);
        if (!nodeToDelete)
        {
            memset(response, 0 , sizeof(response));
            snprintf(response, sizeof(response), "Error: Path not found");
            send(client_socket, response, strlen(response), 0);
            memset(response, 0, sizeof(response));
            return;
        }
        if (deleteNode(nodeToDelete) == 0)
        {
            memset(response, 0 , sizeof(response));
            snprintf(response, sizeof(response), "DELETE DONE");
            send(client_socket, response, strlen(response), 0);
            memset(response, 0, sizeof(response));
        }
        else
        {
            memset(response, 0 , sizeof(response));
            snprintf(response, sizeof(response), "Error deleting node");
            send(client_socket, response, strlen(response), 0);
            memset(response, 0, sizeof(response));
        }
        break;

    case CMD_UNKNOWN:
        memset(response, 0 , sizeof(response));
        snprintf(response, sizeof(response), "Unknown command: %s\nUsage:CREATE|COPY|DELETE <args>", command);
        send(client_socket, response, strlen(response), 0);
        memset(response, 0, sizeof(response));
        break;
    }
}
