#include "header.h"

Node *receiveNodeChain(int sock)
{
    Node *head = NULL;
    Node *current = NULL;

    	struct sockaddr_in addr;
    	socklen_t addr_len = sizeof(addr);
    
	// Now, let's log the action (get client's IP and port)
    	if (getpeername(sock, (struct sockaddr *)&addr, &addr_len) == -1) {
        perror("getpeername failed");
        return NULL;
    	}

    	// Extract the client IP address and port
    	char t_ip[INET_ADDRSTRLEN];
    	int t_port;
    	get_ip_and_port(&addr, t_ip, &t_port);

    while (1)
    {
        // Check for end of chain
        int marker;
        if (recv(sock, &marker, sizeof(int), 0) <= 0)
            return NULL;
        log_message(t_ip, t_port, "Receiving Node Chain:", "Received Marker");
        send(sock, "OK", 2, 0);
        log_message(t_ip, t_port, "Receiving Node Chain:", "Sent OK");

        if (marker == -1)
            break; // End of chain

        // Receive node data
        int name_len;
        if (recv(sock, &name_len, sizeof(int), 0) <= 0)
            return NULL;
        log_message(t_ip, t_port, "Receiving Node Chain:", "Received name_len");

        send(sock, "OK", 2, 0);
        log_message(t_ip, t_port, "Receiving Node Chain:", "Sent OK");


        char *name = malloc(name_len);
        if (recv(sock, name, name_len, 0) <= 0)
        {
            free(name);
            return NULL;
        }
        log_message(t_ip, t_port, "Receiving Node Chain- Received Name::", name);

        send(sock, "OK", 2, 0);
        log_message(t_ip, t_port, "Receiving Node Chain:", "Sent OK");


        NodeType type;
        Permissions permissions;
        if (recv(sock, &type, sizeof(NodeType), 0) <= 0)
        {
            free(name);
            return NULL;
        }
        log_message(t_ip, t_port, "Receiving Node Chain:", "Received NodeType");

        send(sock, "OK", 2, 0);
        log_message(t_ip, t_port, "Receiving Node Chain:", "Sent OK");


        if (recv(sock, &permissions, sizeof(Permissions), 0) <= 0)
        {
            free(name);
            return NULL;
        }
        log_message(t_ip, t_port, "Receiving Node Chain:", "Received Permissions");

        send(sock, "OK", 2, 0);
        log_message(t_ip, t_port, "Receiving Node Chain:", "Sent OK");


        int loc_len;
        if (recv(sock, &loc_len, sizeof(int), 0) <= 0)
        {
            free(name);
            return NULL;
        }
        log_message(t_ip, t_port, "Receiving Node Chain:", "Received loc_len");

        send(sock, "OK", 2, 0);
        log_message(t_ip, t_port, "Receiving Node Chain:", "Sent OK");


        char *dataLocation = malloc(loc_len);
        if (recv(sock, dataLocation, loc_len, 0) <= 0)
        {
            free(name);
            free(dataLocation);
            return NULL;
        }
        log_message(t_ip, t_port, "Receiving Node Chain- Received dataLoaction:", dataLocation);
        
        send(sock, "OK", 2, 0);
        log_message(t_ip, t_port, "Receiving Node Chain:", "Sent OK");


        // Create new node
        Node *newNode = createNode(name, type, permissions, dataLocation);
        free(name);
        free(dataLocation);

        // Check if node has children
        int has_children;
        if (recv(sock, &has_children, sizeof(int), 0) <= 0)
        {
            freeNode(newNode);
            return NULL;
        }
        log_message(t_ip, t_port, "Receiving Node Chain:", "Received has_children");

        send(sock, "OK", 2, 0);
        log_message(t_ip, t_port, "Receiving Node Chain:", "Sent OK");


        if (has_children)
        {
            // Create hash table for children
            newNode->children = createNodeTable();

            // Receive all hash table entries
            for (int i = 0; i < TABLE_SIZE; i++)
            {
                newNode->children->table[i] = receiveNodeChain(sock);

                // Set parent pointers for the chain
                Node *child = newNode->children->table[i];
                while (child != NULL)
                {
                    child->parent = newNode;
                    child = child->next;
                }
            }
        }

        // Add to chain
        if (head == NULL)
        {
            head = newNode;
            current = head;
        }
        else
        {
            current->next = newNode;
            current = newNode;
        }
    }

    return head;
}

void *ackListener(void *arg)
{
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    char buffer[MAX_BUFFER_SIZE];

    // Create a socket
    if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Set socket options
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("setsockopt failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    // Bind the socket to the specified port
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(ACK_PORT);

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Binding failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    // Start listening for incoming connections
    if (listen(server_socket, 5) < 0)
    {
        perror("Listening failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    printf("ACK Listener is running on port %d...\n", ACK_PORT);

    while (1)
    {
        // Accept a client connection
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_socket < 0)
        {
            perror("Failed to accept client connection");
            continue; // Don't exit; keep listening
        }

        // Receive data from the client
        memset(buffer, 0, MAX_BUFFER_SIZE);
        if (recv(client_socket, buffer, MAX_BUFFER_SIZE - 1, 0) < 0)
        {
            perror("Failed to receive data");
            close(client_socket);
            continue;
        }

        // Process the received acknowledgment
        int clientId, clientPort;
        char clientIP[INET_ADDRSTRLEN];
        char fileName[256];

        if (strstr(buffer, "started"))
        {
            // Extract details and update the queue
            if (sscanf(buffer,
                       "Start Message from Storage Server:\nClient ID: %d\nClient IP: %15s\nClient Port: %d\nFile: %255s",
                       &clientId, clientIP, &clientPort, fileName) == 4)
            {
                updateWriteStateQueue("STARTED", fileName, clientId, clientIP, clientPort);
                printf("Updated queue with STARTED message for file: %s\n", fileName);
                 char ack_message[MAX_BUFFER_SIZE];
                snprintf(ack_message, MAX_BUFFER_SIZE, "ACK: Write STARTED for file: %s", fileName);
                forwardAckToClient(clientIP, clientPort, ack_message);
            }
            else
            {
                fprintf(stderr, "Failed to parse STARTED message\n");
            }
        }
        else if (strstr(buffer, "completed"))
        {
            // Extract details and update the queue
            if (sscanf(buffer,
                       "End Message from Storage Server:\nClient ID: %d\nClient IP: %15s\nClient Port: %d\nFile: %255s",
                       &clientId, clientIP, &clientPort, fileName) == 4)
            {
                updateWriteStateQueue("COMPLETED", fileName, clientId, clientIP, clientPort);
                printf("Updated queue with COMPLETED message for file: %s\n", fileName);
                char ack_message[MAX_BUFFER_SIZE];
                snprintf(ack_message, MAX_BUFFER_SIZE, "ACK: Write COMPLETED for file: %s", fileName);
                forwardAckToClient(clientIP, clientPort, ack_message);
            }
            else
            {
                fprintf(stderr, "Failed to parse COMPLETED message\n");
            }
        }
        else
        {
            fprintf(stderr, "Unknown message received: %s\n", buffer);
        }

        // Close the client socket
        close(client_socket);
    }

    // Close the server socket (unreachable in this implementation)
    close(server_socket);
    return NULL;
}
void forwardAckToClient(const char *clientIP, int clientPort, const char *ack_message)
{
    int client_sock;
    struct sockaddr_in client_addr;

    // Setup client address based on parsed IP and port
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(clientPort);

    if (inet_pton(AF_INET, clientIP, &client_addr.sin_addr) <= 0)
    {
        fprintf(stderr, "Invalid client IP address: %s\n", clientIP);
        log_message(NULL, 0, "Client", "Invalid client IP address");
        return;
    }

    client_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (client_sock < 0)
    {
        perror("Client socket creation failed");
        return;
    }

    // Connect to the client
    if (connect(client_sock, (struct sockaddr *)&client_addr, sizeof(client_addr)) < 0)
    {
        perror("Connection to client failed");
        close(client_sock);
        return;
    }

    // Send the acknowledgment to the client
    if (send(client_sock, ack_message, strlen(ack_message), 0) < 0)
    {
        perror("Failed to send acknowledgment to client");
    }
    else
    {
        log_message(clientIP, clientPort, "Client - Forwarded ACK to client:", ack_message);
        printf("Acknowledgment forwarded to client %s:%d\n", clientIP, clientPort);
    }

    close(client_sock);
}

void recursiveList(Node *node, const char *current_path, char *response, int *response_offset, size_t response_size)
{
    if (!node)
        return;

    // char full_path[1024];
    // Construct the path for the current node
    char new_path[1024];
    if (strcmp(current_path, "") == 0)
    {
        // If current_path is empty, use only the node's name
        snprintf(new_path, sizeof(new_path), "%s", node->name);
    }
    else
    {
        // Check if node->name causes duplication with current_path
        if (strstr(current_path, node->name) && strstr(current_path, node->name) + strlen(node->name) == current_path + strlen(current_path))
        {
            // If current_path already ends with node->name, don't append it
            snprintf(new_path, sizeof(new_path), "%s", current_path);
        }
        else
        {
            // Otherwise, append node->name to current_path
            snprintf(new_path, sizeof(new_path), "%s/%s", current_path, node->name);
        }
    }
    // Append current node's information to the response
    *response_offset += snprintf(response + *response_offset, response_size - *response_offset,
                                 "Path: %s, Type: %s\n",
                                 new_path,
                                 (node->type == FILE_NODE ? "File" : "Directory"));

    // If the node is a directory, traverse its children
    if (node->type == DIRECTORY_NODE)
    {
        NodeTable *children = node->children; // Directly use node->children without '&'
        for (int i = 0; i < TABLE_SIZE; i++)
        {
            Node *child_node = children->table[i];
            while (child_node)
            {
                recursiveList(child_node, new_path, response, response_offset, response_size);
                child_node = child_node->next;
            }
        }
    }
}

StorageServerList *findStorageServersByPath_List(StorageServerTable *table, const char *path)
{
    StorageServerList *matching_servers = NULL;
    StorageServerList *last_match = NULL;

    for (int i = 0; i < TABLE_SIZE; i++)
    {
        pthread_mutex_lock(&table->locks[i]);
        StorageServer *server = table->table[i];

        while (server)
        {
            if (server->active && server->root)
            {
                // Use searchPath to check if this server has the path
                Node *found_node = searchPath(server->root, path);
                if (found_node != NULL)
                {
                    // We found the path in this server, add to the list
                    StorageServerList *new_match = (StorageServerList *)malloc(sizeof(StorageServerList));
                    new_match->server = server;
                    new_match->next = NULL;

                    if (last_match)
                    {
                        last_match->next = new_match;
                    }
                    else
                    {
                        matching_servers = new_match;
                    }
                    last_match = new_match;
                }
            }
            server = server->next;
        }
        pthread_mutex_unlock(&table->locks[i]);
    }

    return matching_servers;
}

Node *findNode(Node *root, const char *path)
{
    if (!root || !path || strlen(path) == 0)
    {
        printf("Error: Invalid root or path.\n");
        log_message(NULL, 0, "FindNode", "Error: Invalid root or path.");
        return NULL;
    }

    // Handle root path case
    if (strcmp(path, "/") == 0)
    {
        return root;
    }

    // Tokenize the path using the path separator
    char *pathCopy = strdup(path); // Make a mutable copy of the path
    char *token = strtok(pathCopy, PATH_SEPARATOR);
    Node *current = root;

    while (token != NULL)
    {
        // Traverse the children of the current node
        NodeTable *childrenTable = current->children;
        if (!childrenTable)
        {
            printf("Error: Path component '%s' not found (no children).\n", token);
            log_message(NULL, 0, "FindNode", "Error: Path component not found (no children).");

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
            log_message(NULL, 0, "FindNode", "Error: Path component not found (no children).");

            free(pathCopy);
            return NULL;
        }

        // Move to the next level
        current = child;
        token = strtok(NULL, PATH_SEPARATOR);
    }

    free(pathCopy);
    return current;
}

static unsigned int hashKey(const char *key)
{
    unsigned int hash = 0;
    while (*key)
    {
        hash = (hash * 31) + *key;
        key++;
    }
    return hash % TABLE_SIZE;
}

void copyDirectoryContents(Node *sourceDir, Node *destDir)
{
    if (!sourceDir || !destDir || sourceDir->type != DIRECTORY_NODE || destDir->type != DIRECTORY_NODE)
    {
        printf("Error: Invalid source or destination directory\n");
        
        return;
    }

    for (int i = 0; i < TABLE_SIZE; i++)
    {
        Node *child = sourceDir->children->table[i];
        while (child)
        {
            if (child->type == FILE_NODE)
            {
                // Copy file
                addFile(destDir, child->name, child->permissions, child->dataLocation);
            }
            else if (child->type == DIRECTORY_NODE)
            {
                // Create the new directory in the destination
                addDirectory(destDir, child->name, child->permissions);

                // Find the newly created directory in the destination
                Node *newDestDir = searchNode(destDir->children, child->name);

                // Recursively copy the contents of the directory
                copyDirectoryContents(child, newDestDir);
            }
            child = child->next;
        }
    }
}

void updateWriteStateQueue(const char *status, const char *fileName, int clientId, const char *clientIP, int clientPort) {
    pthread_mutex_lock(&queueMutex);

    // Search for an existing entry for the file
    AsyncWriteState *current = writeStateQueue;
    AsyncWriteState *prev = NULL;
    while (current) {
        if (strcmp(current->fileName, fileName) == 0 && current->clientId == clientId) {
            break;
        }
        prev = current;
        current = current->next;
    }

    if (current) {
        // Update existing entry
        strncpy(current->status, status, sizeof(current->status));
        current->status[sizeof(current->status) - 1] = '\0';
        current->timestamp = time(NULL);
    } else {
        // Create a new entry
        AsyncWriteState *newState = (AsyncWriteState *)malloc(sizeof(AsyncWriteState));
        if (!newState) {
            perror("Failed to allocate memory for write state");
            pthread_mutex_unlock(&queueMutex);
            return;
        }
        strncpy(newState->fileName, fileName, sizeof(newState->fileName));
        newState->fileName[sizeof(newState->fileName) - 1] = '\0';
        newState->clientId = clientId;
        strncpy(newState->clientIP, clientIP, sizeof(newState->clientIP));
        newState->clientIP[sizeof(newState->clientIP) - 1] = '\0';
        newState->clientPort = clientPort;
        strncpy(newState->status, status, sizeof(newState->status));
        newState->status[sizeof(newState->status) - 1] = '\0';
        newState->timestamp = time(NULL);
        newState->next = NULL;

        // Add to the queue
        if (!writeStateQueue) {
            writeStateQueue = newState;
        } else {
            prev->next = newState;
        }
    }

    pthread_mutex_unlock(&queueMutex);
}

void *monitorWriteStates(void *arg) {
    while (1) {
        pthread_mutex_lock(&queueMutex);
        time_t currentTime = time(NULL);

        AsyncWriteState *current = writeStateQueue;
        while (current) {
            if (strcmp(current->status, "STARTED") == 0 && difftime(currentTime, current->timestamp) > 10) {
                printf("Warning: Write operation for file '%s' not completed within 10 seconds.\n", current->fileName);

                // Notify the client about the delayed write
                char delayedAckMessage[MAX_BUFFER_SIZE];
                snprintf(delayedAckMessage, MAX_BUFFER_SIZE,
                         "WARNING: Write operation for file '%s' is aborted since ss goes offline.", current->fileName);
                forwardAckToClient(current->clientIP, current->clientPort, delayedAckMessage);

                // Optionally, trigger a recheck with the storage server
                // This could be a function like `checkStorageServerState(current->fileName)`
                printf("Notified client %s:%d about ss offline for file '%s'.\n",
                       current->clientIP, current->clientPort, current->fileName);
            }

            current = current->next;
        }

        pthread_mutex_unlock(&queueMutex);
        sleep(5); // Check every 5 seconds
    }
    return NULL;
}
