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
    // printf("lock_type = %d\n", node->lock_type);
    node->lock_type = 1; // Set read lock
    // printf("lock_type = %d\n", node->lock_type);
    int fd = open(node->dataLocation, O_RDONLY);
    if (fd < 0)
        return -1;

    lseek(fd, offset, SEEK_SET);
    ssize_t bytes = read(fd, buffer, size);
    close(fd);
    node->lock_type = 0; // Release lock
    // printf("lock_type = %d\n", node->lock_type);

    return bytes;
}

// Helper function to write file in chunks
ssize_t writeFileChunk(Node *node, const char *buffer, size_t size, off_t offset)
{
    // printf("lock_type = %d\n", node->lock_type);
    node->lock_type = 2; // Set write lock
    // printf("lock_type = %d\n", node->lock_type);
    int fd = open(node->dataLocation, O_WRONLY | O_APPEND);
    if (fd < 0)
        return -1;

    lseek(fd, offset, SEEK_SET);
    ssize_t bytes = write(fd, buffer, size);
    close(fd);
    node->lock_type = 0; // Release lock
    // printf("lock_type = %d\n", node->lock_type);

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

void sendAckToNamingServer(const char *status, const char *message, int clientId, const char *fileName, const char *clientIP, int clientPort,char *ip)
{
    int ack_socket;
    struct sockaddr_in naming_server_addr;
    const int naming_server_port = ACK_PORT; // Dedicated port for async write acknowledgments

    // Create a socket
    ack_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (ack_socket < 0)
    {
        perror("Socket creation failed");
        return;
    }

    // Set up the naming server address
    memset(&naming_server_addr, 0, sizeof(naming_server_addr));
    naming_server_addr.sin_family = AF_INET;
    naming_server_addr.sin_port = htons(naming_server_port);

    // Convert IP address to binary form
    if (inet_pton(AF_INET,ip, &naming_server_addr.sin_addr) <= 0)
    {
        perror("Invalid address or address not supported");
        close(ack_socket);
        return;
    }

    // Connect to the naming server
    if (connect(ack_socket, (struct sockaddr *)&naming_server_addr, sizeof(naming_server_addr)) < 0)
    {
        perror("Connection to naming server failed");
        close(ack_socket);
        return;
    }

    // Prepare the acknowledgment message
    char ack_message[512];
    snprintf(ack_message, sizeof(ack_message),
             "%s Message from Storage Server:\n"
             "Client ID: %d\nClient IP: %s\nClient Port: %d\n"
             "File: %s\nMessage: %s\n",
             status, clientId, clientIP, clientPort, fileName, message);
    // Send the acknowledgment message
    if (send(ack_socket, ack_message, strlen(ack_message), 0) < 0)
    {
        perror("Failed to send acknowledgment to naming server");
        close(ack_socket);
        return;
    }

    printf("Acknowledgment sent to naming server:\n%s\n", ack_message);

    // Close the socket
    close(ack_socket);
}

void *flushAsyncWrites(char *ip)
{

    printf("is the function even called\n");
    while (1)
    {
        // pthread_mutex_lock(&queueMutex);

        // Wait until there are tasks in the queue
        while (!asyncWriteQueue)
        {
            pthread_cond_wait(&queueCondition, &queueMutex);
        }

        // Process tasks in the queue
        while (asyncWriteQueue)
        {
            AsyncWriteTask *task = asyncWriteQueue;
            asyncWriteQueue = asyncWriteQueue->next;

            pthread_mutex_unlock(&queueMutex);
            sendAckToNamingServer("Start", "Write operation started for file", task->clientId, task->targetNode->name, task->clientIP, task->clientPort,ip);

            // Simulate writing to persistent storage
            FILE *file = fopen(task->targetNode->dataLocation, "a");
            if (file)
            {
                fwrite(task->data, 1, task->size, file);
                fclose(file);
                printf("Async write completed for file: %s\n", task->targetNode->name);
                sendAckToNamingServer("End", "Write operation completed successfully for file", task->clientId, task->targetNode->name, task->clientIP, task->clientPort,ip);
            }
            else
            {
                perror("Error writing to file");
            }

            // Free the task memory
            free(task->data);
            free(task);

            pthread_mutex_lock(&queueMutex);
        }

        pthread_mutex_unlock(&queueMutex);
    }
    return NULL;
}

int queueAsyncWrite(Node *targetNode, const char *data, size_t size, int client_socket, const char *client_ip, int client_port)
{
    // Validate that the node is a file
    if (targetNode->type != FILE_NODE)
    {
        fprintf(stderr, "Error: Target node is not a file.\n");
        return -1;
    }

    // Allocate memory for the task
    AsyncWriteTask *task = malloc(sizeof(AsyncWriteTask));
    if (!task)
    {
        perror("Failed to allocate memory for async write task");
        return -1;
    }

    // Initialize the task
    task->targetNode = targetNode;
    task->data = malloc(size);
    if (!task->data)
    {
        perror("Failed to allocate memory for task data");
        free(task);
        return -1;
    }
    memcpy(task->data, data, size);
    task->size = size;
    task->clientId = client_socket;
    strncpy(task->clientIP, client_ip, INET_ADDRSTRLEN);
    task->clientIP[INET_ADDRSTRLEN - 1] = '\0'; // Ensure null termination
    task->clientPort = client_port;
    task->writeStatus = 0;
    task->next = NULL;

    // Add task to the queue
    pthread_mutex_lock(&queueMutex);
    if (!asyncWriteQueue)
    {
        asyncWriteQueue = task;
    }
    else
    {
        AsyncWriteTask *current = asyncWriteQueue;
        while (current->next)
        {
            current = current->next;
        }
        current->next = task;
    }

    // Signal the worker thread
    printf("Signaling the worker thread\n");
    pthread_cond_signal(&queueCondition);
    pthread_mutex_unlock(&queueMutex);

    return 0;
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
    int is_sync = 1;

    // Clear any leading/trailing whitespace
    char *cmd_start = input;
    while (*cmd_start == ' ')
        cmd_start++;
    if (strlen(cmd_start) == 0)
    {
        send(client_socket, " \033[1;31mERROR 101:\033[0m \033[38;5;214mInvalid Command: Empty Command!\033[0m\n\0", strlen(" \033[1;31mERROR 101:\033[0m \033[38;5;214mInvalid Command: Empty Command!\033[0m\n\0"), 0);
        return;
    }
    if (sscanf(cmd_start, "%s", command) != 1)
    {
        send(client_socket, " \033[1;31mERROR 101:\033[0m \033[38;5;214mInvalid Command!\033[0m\n\0", strlen(" \033[1;31mERROR 101:\033[0m \033[38;5;214mInvalid Command!\033[0m\n\0"), 0);
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
            send(client_socket, " \033[1;31mERROR 404:\033[0m \033[38;5;214mPath not found!\033[0m\n\0", strlen(" \033[1;31mERROR 404:\033[0m \033[38;5;214mPath not found!\033[0m\n\0"), 0);
            return;
        }

        Node *targetNode = searchPath(root, path);
        if (!targetNode)
        {
            memset(response, 0, sizeof(response));
            snprintf(response, sizeof(response), " \033[1;31mERROR 404:\033[0m \033[38;5;214mPath not found!\033[0m\n\0");
            send(client_socket, response, strlen(response), 0);
            return;
        }

        if (cmd == CMD_READ)
        {
            if (targetNode->lock_type == 2)
            {
                snprintf(response, sizeof(response), "Error: File is being written to\n");
                send(client_socket, response, strlen(response), 0);
                return;
            }
            ssize_t bytes;
            off_t offset = 0;
            struct stat st;
            if ((targetNode->permissions & READ) == 0)
            {
                const char *error = " \033[1;31mERROR 50:\033[0m \033[38;5;214mPermission Denied!\033[0m\n\0";
                send(client_socket, error, strlen(error), 0);
                return;
            }
            if (targetNode->type != FILE_NODE)
            {
                const char *error = " \033[1;31mERROR 51:\033[0m \033[38;5;214mNot a File!\033[0m\n\0";
                send(client_socket, error, strlen(error), 0);
                return;
            }
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
            if (targetNode->lock_type == 2)
            {
                snprintf(response, sizeof(response), "Error: File is being written to\n");
                send(client_socket, response, strlen(response), 0);
                return;
            }
            if (targetNode->lock_type == 1)
            {
                snprintf(response, sizeof(response), "Error: File is being read to\n");
                send(client_socket, response, strlen(response), 0);
                return;
            }
            send(client_socket, "abcdd\n", strlen("abcdd\n"), 0);

            // First receive file size from client
            memset(buffer, 0, sizeof(buffer));
            recv(client_socket, buffer, sizeof(buffer), 0);
            if ((targetNode->permissions & WRITE) == 0)
            {
                const char *error = " \033[1;31mERROR 50:\033[0m \033[38;5;214mPermission Denied!\033[0m\n\0";
                send(client_socket, error, strlen(error), 0);
                return;
            }
            if (targetNode->type != FILE_NODE)
            {
                const char *error = " \033[1;31mERROR 51:\033[0m \033[38;5;214mNot a File!\033[0m\n\0";
                send(client_socket, error, strlen(error), 0);
                return;
            }
            long fileSize;
            int ack_port;
            if (sscanf(buffer, "FILE_SIZE:%ld|ACK_PORT:%d", &fileSize, &ack_port) != 2)
            {
                send(client_socket, " \033[1;31mERROR 46:\033[0m \033[38;5;214mInvalid file size format!\033[0m\n\0",
                     strlen(" \033[1;31mERROR 46:\033[0m \033[38;5;214mInvalid file size format!\033[0m\n\0"), 0);
                return;
            }
            if (fileSize >= 10) // condition that will check whether asynchornous write should happen or not
            {
                is_sync = 0;
            }
            if (strstr(input, "--SYNC"))
            {
                is_sync = 1;
            }

            // Send acknowledgment
            send(client_socket, "READY_TO_RECEIVE\n", strlen("READY_TO_RECEIVE\n"), 0);
            if (is_sync == 1)
            {
                printf("synchornous writing is happening\n");
                // for synchronous writing
                // Receive file content in chunks
                long totalReceived = 0;
                while (totalReceived < fileSize)
                {
                    memset(buffer, 0, sizeof(buffer));
                    ssize_t bytesReceived = recv(client_socket, buffer,
                                                 min(sizeof(buffer), fileSize - totalReceived), 0);

                    if (bytesReceived <= 0)
                    {
                        send(client_socket, " \033[1;31mERROR 56:\033[0m \033[38;5;214mUnable to receive file data!\033[0m\n\0",
                             strlen(" \033[1;31mERROR 56:\033[0m \033[38;5;214mUnable to receive file data!\033[0m\n\0"), 0);
                        return;
                    }

                    if (writeFileChunk(targetNode, buffer, bytesReceived, totalReceived) != bytesReceived)
                    {
                        send(client_socket, " \033[1;31mERROR 57:\033[0m \033[38;5;214mUnable to Write to the file!\033[0m\n\0",
                             strlen(" \033[1;31mERROR 57:\033[0m \033[38;5;214mUnable to Write to the file!\033[0m\n\0"), 0);
                        return;
                    }
                    send(client_socket, "ok\0", 3, 0);
                    totalReceived += bytesReceived;
                }
                memset(buffer, 0, sizeof(buffer));
                recv(client_socket, buffer, sizeof(buffer), 0);
                memset(response, 0, sizeof(response));
                snprintf(response, sizeof(response), "Successfully wrote %ld bytes\n", totalReceived);
                send(client_socket, response, strlen(response), 0);
            }
            else
            {
                printf("Asynchornous writing is happening\n");
                struct sockaddr_in client_addr;
                socklen_t addr_len = sizeof(client_addr);
                if (getpeername(client_socket, (struct sockaddr *)&client_addr, &addr_len) == -1)
                {
                    perror("Error retrieving client IP and port");
                    return;
                }
                char client_ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
                int client_port = ack_port;
                char *asyncDataBuffer = malloc(fileSize);

                if (!asyncDataBuffer)
                {
                    send(client_socket, " \033[1;31mERROR 58:\033[0m \033[38;5;214mMemory allocation failed!\033[0m\n\0",
                         strlen(" \033[1;31mERROR 58:\033[0m \033[38;5;214mMemory allocation failed!\033[0m\n\0"), 0);
                    return;
                }

                long totalReceived = 0;
                while (totalReceived < fileSize)
                {
                    memset(buffer, 0, sizeof(buffer));
                    ssize_t bytesReceived = recv(client_socket, buffer,
                                                 min(sizeof(buffer), fileSize - totalReceived), 0);

                    if (bytesReceived <= 0)
                    {
                        free(asyncDataBuffer);
                        send(client_socket, " \033[1;31mERROR 56:\033[0m \033[38;5;214mUnable to receive file data.\033[0m\n\0",
                             strlen(" \033[1;31mERROR 56:\033[0m \033[38;5;214mUnable to receive file data.\033[0m\n\0"), 0);
                        return;
                    }
                    send(client_socket, "ok\0", 3, 0);

                    // Copy data to the async buffer
                    memcpy(asyncDataBuffer + totalReceived, buffer, bytesReceived);
                    totalReceived += bytesReceived;
                }
                memset(buffer, 0, sizeof(buffer));
                recv(client_socket, buffer, sizeof(buffer), 0);

                send(client_socket, "ACK: WRITE REQUEST ACCEPTED\n", strlen("ACK: WRITE REQUEST ACCEPTED\n"), 0);

                // Queue the data for asynchronous write
                if (queueAsyncWrite(targetNode, asyncDataBuffer, fileSize, client_socket, client_ip, client_port) != 0)
                {
                    free(asyncDataBuffer);
                    send(client_socket, " \033[1;31mERROR 90:\033[0m \033[38;5;214mFailed to queue asynchronous write!\033[0m\n\0", strlen(" \033[1;31mERROR 90:\033[0m \033[38;5;214mFailed to queue asynchronous write!\033[0m\n\0"), 0);
                    return;
                }

                // Notify the client that the asynchronous write has been queued
                // snprintf(response, sizeof(response), "Asynchronous write of %ld bytes queued successfully\n", fileSize);
                // send(client_socket, response, strlen(response), 0);
            }
        }
        else if (cmd == CMD_META)
        {
            if (getFileMetadata(targetNode, &metadata) == 0)
            {
                char permissions[64];
                getPermissionsString(metadata.st_mode & 0777, permissions, sizeof(permissions));
                memset(response, 0, sizeof(response));
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
                send(client_socket, " \033[1;31mERROR 30:\033[0m \033[38;5;214mUnable to get MetaData.\033[0m\n\0",
                     strlen(" \033[1;31mERROR 30:\033[0m \033[38;5;214mUnable to get MetaData.\033[0m\n\0"), 0);
            }
        }
        else if (cmd == CMD_STREAM)
        {
            off_t offset = 0;
            int chunks = 0;
            ssize_t bytes;
            if ((targetNode->permissions & READ) == 0)
            {
                const char *error = " \033[1;31mERROR 50:\033[0m \033[38;5;214mPermission Denied!\033[0m\n\0";
                send(client_socket, error, strlen(error), 0);
                return;
            }
            if (targetNode->type != FILE_NODE)
            {
                const char *error = " \033[1;31mERROR 51:\033[0m \033[38;5;214mNot a File!\033[0m\n\0";
                send(client_socket, error, strlen(error), 0);
                return;
            }
            send(client_socket, "START_STREAM\n", strlen("START_STREAM\n"), 0);

            while ((bytes = streamAudioFile(targetNode, buffer, CHUNK_SIZE, offset)) > 0)
            {
                if (send(client_socket, buffer, bytes, 0) != bytes)
                {
                    send(client_socket, " \033[1;31mERROR 31:\033[0m \033[38;5;214mUnable to Stream Data!\033[0m\n\0",
                         strlen(" \033[1;31mERROR 31:\033[0m \033[38;5;214mUnable to Stream Data!\033[0m\n\0"), 0);
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
            send(client_socket, " \033[1;31mERROR 404:\033[0m \033[38;5;214mPath is needed!\033[0m\n\0", strlen(" \033[1;31mERROR 404:\033[0m \033[38;5;214mPath is needed!\033[0m\n\0"), 0);
            return;
        }
        Node *parentDir = findNode(root, path);
        if (!parentDir)
        {
            memset(response, 0, sizeof(response));
            snprintf(response, sizeof(response), " \033[1;31mERROR 100:\033[0m \033[38;5;214mParent Directory Missing!\033[0m\n\0");
            send(client_socket, response, strlen(response), 0);
            memset(response, 0, sizeof(response));
            return;
        }

        NodeType type = FILE_NODE;
        if (createEmptyNode(parentDir, name, type))
        {
            memset(response, 0, sizeof(response));
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
            memset(response, 0, sizeof(response));
            snprintf(response, sizeof(response), " \033[1;31mERROR 32:\033[0m \033[38;5;214mUnable to create node!\033[0m\n\0");
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
            send(client_socket, " \033[1;31mERROR 404:\033[0m \033[38;5;214mPath is needed!\033[0m\n\0", strlen(" \033[1;31mERROR 404:\033[0m \033[38;5;214mPath is needed!\033[0m\n\0"), 0);
            return;
        }
        parentDir = findNode(root, path);
        if (!parentDir)
        {
            memset(response, 0, sizeof(response));
            snprintf(response, sizeof(response), " \033[1;31mERROR 100:\033[0m \033[38;5;214mParent Disrectory Missing!\033[0m\n\0");
            send(client_socket, response, strlen(response), 0);
            memset(response, 0, sizeof(response));
            return;
        }

        type = DIRECTORY_NODE;
        if (createEmptyNode(parentDir, name2, type))
        {
            memset(response, 0, sizeof(response));

            snprintf(response, sizeof(response), "CREATE DONE");
            send(client_socket, response, strlen(response), 0);
            memset(response, 0, sizeof(response));
            return;
        }
        else
        {
            memset(response, 0, sizeof(response));
            snprintf(response, sizeof(response), " \033[1;31mERROR 44:\033[0m \033[38;5;214mFailed to Create!\033[0m\n\0");
            send(client_socket, response, strlen(response), 0);
            memset(response, 0, sizeof(response));
            return;
        }
        break;

    case CMD_UNKNOWN:
        send(client_socket, " \033[1;31mERROR 101:\033[0m \033[38;5;214mUnknown command: %s\nUsage: READ|WRITE|META|STREAM <args>\n\033[0m\n\0", strlen(" \033[1;31mERROR 101:\033[0m \033[38;5;214mUnknown command: %s\nUsage: READ|WRITE|META|STREAM <args>\n\033[0m\n\0"), 0);
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
        send(client_socket, " \033[1;31mERROR 101:\033[0m \033[38;5;214mInvalid Command: It is empty!\033[0m\n\0", strlen(" \033[1;31mERROR 101:\033[0m \033[38;5;214mInvalid Command: It is empty!\033[0m\n\0"), 0);
        return;
    }
    if (sscanf(cmd_start, "%s", command) != 1)
    {
        send(client_socket, " \033[1;31mERROR 101:\033[0m \033[38;5;214mInvalid Command Format!\033[0m\n\0", strlen(" \033[1;31mERROR 101:\033[0m \033[38;5;214mInvalid Command Format!\033[0m\n\0"), 0);
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
        // char ack[100001];
        // send(client_socket, "lala lala\n", strlen("lala lala\n"), 0);
        // recv(client_socket,ack,sizeof(ack),0);
        if (sscanf(cmd_start, "%s %d %s", typeStr, &temp, path) != 3)
        {
            memset(response, 0, sizeof(response));
            snprintf(response, sizeof(response), " \033[1;31mERROR 101:\033[0m \033[38;5;214mInvalid Command: Type and path are required!\033[0m\n\0");
            send(client_socket, response, strlen(response), 0);
            memset(response, 0, sizeof(response));
            return;
        }
        char *lastSlash = strrchr(path, '/');
        if (!lastSlash)
        {
            memset(response, 0, sizeof(response));
            snprintf(response, sizeof(response), " \033[1;31mERROR 404:\033[0m \033[38;5;214mInvalid Path Format!\033[0m\n\0");
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
            memset(response, 0, sizeof(response));
            snprintf(response, sizeof(response), " \033[1;31mERROR 100:\033[0m \033[38;5;214mParent Directory Missing!\033[0m\n\0");
            send(client_socket, response, strlen(response), 0);
            memset(response, 0, sizeof(response));
            return;
        }

        NodeType type = (strcasecmp(typeStr, "DIR") == 0) ? DIRECTORY_NODE : FILE_NODE;
        if (createEmptyNode(parentDir, name, type))
        {
            memset(response, 0, sizeof(response));
            printf("hillo\n");
            fflush(stdout);
            snprintf(response, sizeof(response), "CREATE DONE");
            if (send(client_socket, response, strlen(response), 0) < 0)
            {
                perror("hajnckm");
            }
            printf("sent\n");
            fflush(stdout);
            memset(response, 0, sizeof(response));
            return;
        }
        else
        {
            memset(response, 0, sizeof(response));
            snprintf(response, sizeof(response), " \033[1;31mERROR 32:\033[0m \033[38;5;214mUnable to create node!\033[0m\n\0");
            send(client_socket, response, strlen(response), 0);
            memset(response, 0, sizeof(response));
        }
        break;

    case CMD_COPY:
        sscanf(cmd_start, "%s %s", path, secondPath);
        memset(buffer, 0, sizeof(buffer));
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
        memset(buffer, 0, sizeof(buffer));
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
            memset(response, 0, sizeof(response));
            snprintf(response, sizeof(response), " \033[1;31mERROR 404:\033[0m \033[38;5;214mMissing Path argument!\033[0m\n\0");
            send(client_socket, response, strlen(response), 0);
            memset(response, 0, sizeof(response));
            return;
        }
        Node *nodeToDelete = searchPath(root, path);
        if (!nodeToDelete)
        {
            memset(response, 0, sizeof(response));
            snprintf(response, sizeof(response), " \033[1;31mERROR 404:\033[0m \033[38;5;214mPath not found!\033[0m\n\0");
            send(client_socket, response, strlen(response), 0);
            memset(response, 0, sizeof(response));
            return;
        }
        if (deleteNode(nodeToDelete) == 0)
        {
            memset(response, 0, sizeof(response));
            snprintf(response, sizeof(response), "DELETE DONE");
            send(client_socket, response, strlen(response), 0);
            memset(response, 0, sizeof(response));
        }
        else
        {
            memset(response, 0, sizeof(response));
            snprintf(response, sizeof(response), " \033[1;31mERROR 33:\033[0m \033[38;5;214mUnable to delete node!\033[0m\n\0");
            send(client_socket, response, strlen(response), 0);
            memset(response, 0, sizeof(response));
        }
        break;

    case CMD_UNKNOWN:
        memset(response, 0, sizeof(response));
        snprintf(response, sizeof(response), " \033[1;31mERROR 101:\033[0m \033[38;5;214mUnknown command: %s\nUsage: READ|WRITE|META|STREAM <args>\n\033[0m\n\0", command);
        send(client_socket, response, strlen(response), 0);
        memset(response, 0, sizeof(response));
        break;
    }
}
