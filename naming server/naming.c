#include "header.h"

void logEvent(const char *level, const char *ip, int port, const char *message)
{
    FILE *log_file = fopen(LOG_FILE, "a");
    if (!log_file)
    {
        perror("Failed to open log file");
        return;
    }

    // Get current timestamp
    time_t now = time(NULL);
    struct tm *local_time = localtime(&now);
    char timestamp[20];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", local_time);

    // Use "N/A" if IP is NULL
    const char *log_ip = (ip != NULL) ? ip : "N/A";

    // Write log entry
    fprintf(log_file, "[%s] [%s] [IP: %s:%d] %s\n", timestamp, level, log_ip, port, message);
    printf("[%s] [%s] [IP: %s:%d] %s\n", timestamp, level, log_ip, port, message); // Optional console output

    fclose(log_file);
}
StorageServerTable *createStorageServerTable()
{
    StorageServerTable *table = (StorageServerTable *)malloc(sizeof(StorageServerTable));
    for (int i = 0; i < TABLE_SIZE; i++)
    {
        table->table[i] = NULL;
        pthread_mutex_init(&table->locks[i], NULL);
    }
    table->count = 0;
    return table;
}

// Hash function for storage server (using IP and port)
unsigned int hashStorageServer(const char *ip, int port)
{
    unsigned int hash = 0;
    while (*ip)
    {
        hash = (hash * 31) + *ip;
        ip++;
    }
    hash = (hash * 31) + port;
    return hash % TABLE_SIZE;
}

// Add storage server to hash table
void addStorageServer(StorageServerTable *table, StorageServer *server)
{
    unsigned int index = hashStorageServer(server->ip, server->nm_port);

    pthread_mutex_lock(&table->locks[index]);
    server->next = table->table[index];
    table->table[index] = server;
    table->count++;
    pthread_mutex_unlock(&table->locks[index]);
}

// Find storage server in hash table
StorageServer *findStorageServer(StorageServerTable *table, const char *ip, int port)
{
    unsigned int index = hashStorageServer(ip, port);

    pthread_mutex_lock(&table->locks[index]);
    StorageServer *current = table->table[index];
    while (current)
    {
        if (strcmp(current->ip, ip) == 0 && current->nm_port == port)
        {
            pthread_mutex_unlock(&table->locks[index]);
            return current;
        }
        current = current->next;
    }
    pthread_mutex_unlock(&table->locks[index]);
    return NULL;
}

// Find storage server containing a specific path
StorageServer *findStorageServerByPath(StorageServerTable *table, const char *path)
{
    // No need for path copy and tokenization since searchPath handles that
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
                    // We found the path in this server
                    pthread_mutex_unlock(&table->locks[i]);
                    return server;
                }
            }
            server = server->next;
        }
        pthread_mutex_unlock(&table->locks[i]);
    }

    return NULL;
}

// Handle new storage server connection
StorageServer *handleNewStorageServer(int socket, StorageServerTable *table)
{
    StorageServer *server = malloc(sizeof(StorageServer));
    server->socket = socket;
    server->active = true;
    pthread_mutex_init(&server->lock, NULL);

    // Receive server information
    if (receiveServerInfo(socket, server->ip, &server->nm_port, &server->client_port, &server->root) != 0)
    {
        free(server);
        return NULL;
    }

    addStorageServer(table, server);
    return server;
}

// Thread function to handle storage server
void *storageServerHandler(void *arg)
{
    StorageServer *server = (StorageServer *)arg;

    while (1)
    {
        char command[1024];
        memset(command, 0,sizeof(command));
        ssize_t bytes_received = recv(server->socket, command, sizeof(command) - 1, 0);

        if (bytes_received <= 0)
        {
            pthread_mutex_lock(&server->lock);
            server->active = false;
            pthread_mutex_unlock(&server->lock);
            printf("Storage server %s disconnected\n", server->ip);
            break;
        }

        command[bytes_received] = '\0';
        // Handle storage server commands/updates
        // Update the server's node tree as needed
    }

    return NULL;
}

void getFileName(const char *path, char **filename)
{
    if (!path || !filename)
    {
        printf("Error: Invalid path or filename pointer.\n");
        return;
    }

    const char *lastSlash = strrchr(path, '/');
    *filename = lastSlash ? (char *)(lastSlash + 1) : (char *)path;
}

void *clientHandler(void *arg)
{
    struct
    {
        int socket;
        StorageServerTable *table;
    } *args = arg;
    int client_socket = args->socket;
    StorageServerTable *table = args->table;
    char buffer[MAX_BUFFER_SIZE];
    char command[20];
    char path[MAX_PATH_LENGTH];
    char dest_path[MAX_PATH_LENGTH];
    ssize_t bytes_received;
    while (1)
    {
        memset(buffer, 0 , sizeof(buffer));
        bytes_received = recv(client_socket, buffer, sizeof(buffer), 0);
        buffer[bytes_received] = '\0';
        if (sscanf(buffer, "%s %s", command, path) < 1)
        {
            send(client_socket, "Error: Invalid command format\n",
                 strlen("Error: Invalid command format\n"), 0);
            continue;
        }
        for (int i = 0; command[i]; i++)
        {
            command[i] = toupper(command[i]);
        }
        printf("%s \n", path);
        if (strcmp(command, "READ") == 0 || strcmp(command, "WRITE") == 0 || strcmp(command, "META") == 0 || strcmp(command, "STREAM") == 0)
        {
            StorageServer *server = findStorageServerByPath(table, path);
            printf("%s\n", server->root->name);
            if (!server)
            {
                const char *error = "Path not found in any storage server ";
                send(client_socket, error, strlen(error), 0);
                continue;
            }
            pthread_mutex_lock(&server->lock);
            char response[MAX_BUFFER_SIZE];
            if (server->active)
            {
                printf("storage details %s %d\n", server->ip, server->client_port);
                memset(response, 0 , sizeof(response));
                snprintf(response, sizeof(response), "StorageServer: %s : %d", server->ip, server->client_port);
                send(client_socket, response, strlen(response), 0);
            }
            else
            {
                const char *error = "Storage server is not active";
                send(client_socket, error, strlen(error), 0);
            }
            pthread_mutex_unlock(&server->lock);
        }
        else if (sscanf(buffer, "LIST %s", path) == 1 || strcmp(buffer, "LIST") == 0)
        {
            char response[100001];
            int response_offset = 0;
            if (strcmp(buffer, "LIST") == 0)
            {
                for (int i = 0; i < TABLE_SIZE; i++)
                {
                    pthread_mutex_lock(&table->locks[i]);
                    StorageServer *server = table->table[i];
                    while (server)
                    {
                        if (server->active)
                        {
                            // Traverse the entire structure of this server
                            recursiveList(server->root, "", response, &response_offset, sizeof(response));
                        }
                        server = server->next;
                    }
                    pthread_mutex_unlock(&table->locks[i]);
                }
                // Send the response with all the matching servers
                if (response_offset > 0)
                {
                    send(client_socket, response, response_offset, 0); // Send the listing response to the client
                }
                else
                {
                    const char *error = "No files or directories found in the specified path across all servers.";
                    send(client_socket, error, strlen(error), 0);
                }
            }
            else
            {
                StorageServerList *servers = findStorageServersByPath_List(table, path);
                if (!servers)
                {
                    const char *error = "Path not found in any storage server";
                    send(client_socket, error, strlen(error), 0);
                    continue;
                }

                // Iterate over all matching servers
                for (StorageServerList *server_list = servers; server_list != NULL; server_list = server_list->next)
                {
                    StorageServer *server = server_list->server;
                    pthread_mutex_lock(&server->lock);
                    if (server->active)
                    {
                        // The path has been found in this server, now find the specified path inside the server
                        Node *target_node = searchPath(server->root, path);
                        if (!target_node)
                        {
                            const char *error = "Specified path not found in the storage server";
                            send(client_socket, error, strlen(error), 0);
                            pthread_mutex_unlock(&server->lock);
                            continue;
                        }
                        else
                        {
                            recursiveList(target_node, path, response, &response_offset, sizeof(response));
                        }

                        // If the path is a directory, list its immediate children
                    }
                    else
                    {
                        const char *error = "Storage server is not active";
                        send(client_socket, error, strlen(error), 0);
                    }
                    pthread_mutex_unlock(&server->lock);
                }

                // Send the response with all the matching servers
                if (response_offset > 0)
                {
                    send(client_socket, response, response_offset, 0); // Send the listing response to the client
                }
                else
                {
                    const char *error = "No files or directories found in the specified path across all servers.";
                    send(client_socket, error, strlen(error), 0);
                }

                // Free the list of servers
                while (servers)
                {
                    StorageServerList *tmp = servers;
                    servers = servers->next;
                    free(tmp);
                }
            }
        }

        else if (strcmp(command, "CREATE") == 0 || strcmp(command, "DELETE") == 0 || strcmp(command, "COPY") == 0)
        {
            // send(client_socket,command,sizeof(command),0);
            char type[5];
            int ss_num = -1;
            if (sscanf(buffer, "CREATE %s %d %s", type, &ss_num, path) == 3)
            {
                if (strcmp(type, "FILE") == 0 || strcmp(type, "DIR") == 0)
                {
                    printf("%d \n", ss_num);
                    StorageServer *server;
                    for (int i = 0; i < TABLE_SIZE && ss_num != 0; i++)
                    {
                        pthread_mutex_lock(&table->locks[i]);
                        server = table->table[i];
                        if (server)
                        {
                            ss_num--;
                        }
                        pthread_mutex_unlock(&table->locks[i]);
                    }
                    if (server != NULL)
                    {
                        printf("hii\n");
                        pthread_mutex_lock(&server->lock);
                        if (server->active)
                        {
                            char respond[100001];
                            send(server->socket, buffer, strlen(buffer), 0);
                            memset(respond, 0 , sizeof(respond));
                            recv(server->socket, respond, sizeof(respond), 0);
                            printf("%s\n", respond);
                            if (strncmp(respond, "CREATE DONE", 11) == 0)
                            {
                                printf("yeahhh\n");
                                char *lastSlash = strrchr(path, '/');
                                if (!lastSlash)
                                {
                                    send(client_socket, "Error: Invalid path format", strlen("Error: Invalid path format"), 0);
                                    return NULL;
                                }
                                *lastSlash = '\0';
                                char *name = lastSlash + 1;
                                Node *parentDir = searchPath(server->root, path);
                                *lastSlash = '/';
                                if (!parentDir)
                                {
                                    send(client_socket, "Error: Parent directory not found", strlen("Error: Parent directory not found"), 0);
                                    return NULL;
                                }
                                NodeType typ;
                                if (strcmp(type, "DIR") == 0)
                                {
                                    typ = DIRECTORY_NODE;
                                }
                                else
                                {
                                    typ = FILE_NODE;
                                }
                                Node *newNode = createNode(name, typ, READ | WRITE, path);
                                newNode->parent = parentDir;
                                insertNode(parentDir->children, newNode);
                            }
                            send(client_socket, respond, strlen(respond), 0);
                        }
                        else
                        {
                            const char *error = "Storage server is not active";
                            send(client_socket, error, strlen(error), 0);
                        }
                        pthread_mutex_unlock(&server->lock);
                    }
                    else
                    {
                        const char *error = "No, such storage server exit";
                        send(client_socket, error, strlen(error), 0);
                    }
                }
                else
                {
                    send(client_socket, "Error: Invalid CREATE type\n", strlen("Error: Invalid CREATE type\n"), 0);
                }
                // }
            }
            else if (sscanf(buffer, "DELETE %s", path) == 1)
            {
                StorageServer *server = findStorageServerByPath(table, path);
                if (!server)
                {
                    const char *error = "Path not found in any storage server";
                    send(client_socket, error, strlen(error), 0);
                    continue;
                }
                else
                {
                    printf("hiiii delete");
                    pthread_mutex_lock(&server->lock);
                    if (server->active)
                    {
                        char respond[100001];
                        printf("server root %s\n", server->root->name);
                        send(server->socket, buffer, strlen(buffer), 0);
                        memset(respond, 0, sizeof(respond));
                        recv(server->socket, respond, sizeof(respond), 0);
                        printf("%s\n", respond);
                        if (strcmp(respond, "DELETE DONE") == 0)
                        {
                            Node *nodeToDelete = searchPath(server->root, path);
                            deleteNode(nodeToDelete);
                        }
                        send(client_socket, respond, strlen(respond), 0);
                    }
                    else
                    {
                        const char *error = "Storage server is not active";
                        send(client_socket, error, strlen(error), 0);
                    }
                    pthread_mutex_unlock(&server->lock);
                }
            }
            else if (sscanf(buffer, "COPY %s %s", path, dest_path) == 2)
            {
                StorageServer *source_server = findStorageServerByPath(table, path);
                // printf("%s\n", source_server->root->name);
                if (!source_server)
                {
                    const char *error = "Source path does not exist in any storage server";
                    send(client_socket, error, strlen(error), 0);
                    continue;
                }
                Node *source_node = findNode(source_server->root, path);
                if (!source_node)
                {
                    const char *error = "Source path not found";
                    send(client_socket, error, strlen(error), 0);
                    continue;
                }
                // printf("bansal maa ka loda\n");
                StorageServer *dest_server = findStorageServerByPath(table, dest_path);
                if (!dest_server)
                {
                    // Destination server not found, check if parent directory exists
                    char parent_path[MAX_PATH_LENGTH];
                    getParentPath(dest_path, parent_path);

                    dest_server = findStorageServerByPath(table, parent_path);
                    if (!dest_server)
                    {
                        const char *error = "Destination path invalid";
                        send(client_socket, error, strlen(error), 0);
                        continue;
                    }

                    // Check if parent directory exists and is actually a directory
                    Node *parent_node = findNode(dest_server->root, parent_path);
                    if (!parent_node || parent_node->type != DIRECTORY_NODE)
                    {
                        const char *error = "Destination parent path is not a directory";
                        send(client_socket, error, strlen(error), 0);
                        continue;
                    }
                    else
                    {
                        char init_cmd[MAX_BUFFER_SIZE];
                        memset(buffer,0,sizeof(buffer));
                        snprintf(buffer,sizeof(buffer),"COPY %s %s",path,parent_path);
                        send(source_server->socket, buffer, strlen(buffer), 0);
                        memset(init_cmd, 0 , sizeof(init_cmd));
                        recv(source_server->socket, init_cmd, sizeof(init_cmd), 0);
                        char server_info[MAX_BUFFER_SIZE];
                        memset(server_info, 0 , sizeof(server_info));
                        snprintf(server_info, sizeof(server_info), "SOURCE SERVER_INFO %s %d",
                                 dest_server->ip, dest_server->client_port);
                        send(source_server->socket, server_info, strlen(server_info), 0);
                        char response[100001];
                        memset(response, 0 , sizeof(response));
                        recv(source_server->socket, response, sizeof(response), 0);
                        if (strncmp(response, "COPY DONE",9)==0)
                        {
                            //implement logic to copy files and directrix from the source hast table to dest in the directrix we are copying the data
                            if (source_node->type == DIRECTORY_NODE)
                            {
                                Node *destParentNode = findNode(dest_server->root, parent_path);
                                if (!destParentNode || destParentNode->type != DIRECTORY_NODE)
                                {
                                    printf("Error: Destination path is not a valid directory\n");
                                    const char *error = "Destination path is not a valid directory";
                                    send(client_socket, error, strlen(error), 0);
                                    continue;
                                }
                                addDirectory(destParentNode, source_node->name, source_node->permissions);
                                Node *newRootDir = searchNode(destParentNode->children, source_node->name);
                                // Copy the contents of the source directory to the destination directory
                                copyDirectoryContents(source_node, destParentNode);

                                const char *success = "Directory copied successfully";
                                send(client_socket, success, strlen(success), 0);
                            }
                            else
                            {
                                // Handle single file copy (using previous implementation)
                                // char parent_path[MAX_PATH_LENGTH];
                                // getParentPath(dest_path, parent_path);
                                Node *destParentNode = findNode(dest_server->root, dest_path);
                                addFile(destParentNode, source_node->name, source_node->permissions, source_node->dataLocation);
                                    // handleCopyOperation(source_node, destParentNode, dest_path);
                                const char *success = "File copied successfully";
                                send(client_socket, success, strlen(success), 0);
                            }
                        }
                        send(client_socket, response, strlen(response), 0);
                    }
                }
                else
                {
                    Node *dest_node = findNode(dest_server->root, dest_path);
                    if (dest_node->type == FILE_NODE)
                    {
                        const char *error = "Destination parent path is not a directory";
                        send(client_socket, error, strlen(error), 0);
                        continue;
                    }
                    else
                    {
                        printf("hello\n");
                        char init_cmd[MAX_BUFFER_SIZE];
                        send(source_server->socket, buffer, strlen(buffer), 0);
                        memset(init_cmd, 0, sizeof(init_cmd));
                        recv(source_server->socket, init_cmd, sizeof(init_cmd), 0);
                        char server_info[MAX_BUFFER_SIZE];
                        memset(server_info , 0  , sizeof(server_info));
                        snprintf(server_info, sizeof(server_info), "SOURCE SERVER_INFO %s %d",
                                 dest_server->ip, dest_server->client_port);
                        send(source_server->socket, server_info, strlen(server_info), 0);
                        char response[100001];
                        memset(response, 0 , sizeof(response));
                        recv(source_server->socket, response, sizeof(response), 0);
                        if (strncmp(response, "COPY DONE", 9) == 0)
                        {
                            // implement logic to copy files and directrix from the source hast table to dest in the directrix we are copying the data
                            if (source_node->type == DIRECTORY_NODE)
                            {
                                printf("%s\n",dest_path);
                                Node *destParentNode = findNode(dest_server->root, dest_path);
                                if (!destParentNode || destParentNode->type != DIRECTORY_NODE)
                                {
                                    printf("Error: Destination path is not a valid directory\n");
                                    const char *error = "Destination path is not a valid directory";
                                    send(client_socket, error, strlen(error), 0);
                                    continue;
                                }
                                addDirectory(destParentNode, source_node->name, source_node->permissions);
                                Node *newRootDir = searchNode(destParentNode->children, source_node->name);
                                // Copy the contents of the source directory to the destination directory
                                copyDirectoryContents(source_node, newRootDir);

                                const char *success = "Directory copied successfully";
                                send(client_socket, success, strlen(success), 0);
                            }
                            else
                            {
                                // Handle single file copy (using previous implementation)
                                // char parent_path[MAX_PATH_LENGTH];
                                // getParentPath(dest_path, parent_path);
                                Node *destParentNode = findNode(dest_server->root, dest_path);
                                addFile(destParentNode,source_node->name,source_node->permissions,source_node->dataLocation);
                                // handleCopyOperation(source_node, destParentNode, dest_path);
                                const char *success = "File copied successfully";
                                send(client_socket, success, strlen(success), 0);
                            }
                        }
                        send(client_socket, response, strlen(response), 0);
                    }
                }
            }
        }
        else if (strcmp(command, "EXIT") == 0)
        {
            break;
        }
        else
        {
            send(client_socket, "Error: Unknown command\n",
                 strlen("Error: Unknown command\n"), 0);
        }
    }
}

// Function to receive all server information
int receiveServerInfo(int sock, char *ip_out, int *nm_port_out, int *client_port_out, Node **root_out)
{
    char buffer[1024];
    int bytes_received;
    memset(buffer, 0 , sizeof(buffer));
    bytes_received = recv(sock, buffer, sizeof(buffer), 0);
    if (bytes_received <= 0)
    {
        perror("Failed to receive data");
        return -1;
    }
    // memset(buffer, 0 , sizeof(buffer));
    memcpy(ip_out, buffer, 16);
    memcpy(nm_port_out, buffer + 16, sizeof(int));
    memcpy(client_port_out, buffer + 16 + sizeof(int), sizeof(int));
    ip_out[15] = '\0';
    send(sock, buffer, sizeof(buffer), 0);
    *root_out = receiveNodeChain(sock);
    if (*root_out == NULL)
        return -1;

    return 0;
}

void *storageServerAcceptor(void *arg)
{
    AcceptorArgs *args = (AcceptorArgs *)arg;
    struct sockaddr_in storage_addr = args->server_addr;
    socklen_t storage_addrlen = sizeof(storage_addr);
    int storage_server_fd = args->server_fd;
    StorageServerTable *server_table = args->server_table;
    while (1)
    {
        // Accept new storage server connection
        int storage_sock = accept(storage_server_fd, (struct sockaddr *)&storage_addr, &storage_addrlen);
        if (storage_sock < 0)
        {
            perror("Storage accept failed");
            continue; // Continue accepting other connections even if one fails
        }

        printf("New storage server connected.\n");

        // Handle the new storage server connection in a separate thread
        StorageServer *server = handleNewStorageServer(storage_sock, server_table);
        if (!server)
        {
            printf("Failed to handle new storage server connection.\n");
            close(storage_sock);
            continue;
        }

        // Create a new thread to handle this storage server
        pthread_t server_thread;
        if (pthread_create(&server_thread, NULL, storageServerHandler, server) != 0)
        {
            perror("Failed to create storage server handler thread");
            pthread_mutex_lock(&server->lock);
            server->active = false;
            pthread_mutex_unlock(&server->lock);
            close(storage_sock);
            continue;
        }

        // Detach the thread so it can clean up itself when done
        pthread_detach(server_thread);

        printf("Storage server successfully registered:\n");
        printf("IP: %s\n", server->ip);
        printf("Naming Port: %d\n", server->nm_port);
        printf("Client Port: %d\n", server->client_port);
        if (server->root)
        {
            printf("Root directory: %s\n", server->root->name);
        }
        printf("Total storage servers connected: %d\n", server_table->count);
    }

    return NULL;
}

pthread_t ackListenerThread;    

int main()
{
    StorageServerTable *server_table = createStorageServerTable();
    int storage_server_fd, naming_server_fd;
    struct sockaddr_in storage_addr, naming_addr;
    int opt = 1;

    // Initialize storage server socket
    if ((storage_server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("Storage socket creation failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(storage_server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT,
                   &opt, sizeof(opt)))
    {
        perror("Set socket options failed");
        exit(EXIT_FAILURE);
    }

    storage_addr.sin_family = AF_INET;
    storage_addr.sin_addr.s_addr = INADDR_ANY;
    storage_addr.sin_port = htons(STORAGE_PORT);

    if (bind(storage_server_fd, (struct sockaddr *)&storage_addr,
             sizeof(storage_addr)) < 0)
    {
        perror("Storage bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(storage_server_fd, 10) < 0)
    {
        perror("Storage listen failed");
        exit(EXIT_FAILURE);
    }

    AcceptorArgs *args = malloc(sizeof(AcceptorArgs));
    args->server_fd = storage_server_fd;
    args->server_addr = storage_addr;
    args->server_table = server_table;

    // Create thread for accepting storage servers
    pthread_t storage_acceptor_thread;
    if (pthread_create(&storage_acceptor_thread, NULL, storageServerAcceptor, args) != 0)
    {
        perror("Failed to create storage server acceptor thread");
        free(args);
        close(storage_server_fd);
        return -1;
    }

    // Detach the acceptor thread
    pthread_detach(storage_acceptor_thread);

    if (pthread_create(&ackListenerThread, NULL, ackListener, NULL) != 0)
    {
        perror("Failed to create acknowledgment listener thread");
        exit(EXIT_FAILURE);
    }

    printf("Acknowledgment listener thread started.\n");

    // Detach the thread to allow independent execution
    pthread_detach(ackListenerThread);

    printf("Storage server acceptor started on port %d\n", STORAGE_PORT);

    if ((naming_server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("Naming socket creation failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(naming_server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT,
                   &opt, sizeof(opt)))
    {
        perror("Set naming socket options failed");
        exit(EXIT_FAILURE);
    }

    naming_addr.sin_family = AF_INET;
    naming_addr.sin_addr.s_addr = INADDR_ANY;
    naming_addr.sin_port = htons(NAMING_PORT);

    if (bind(naming_server_fd, (struct sockaddr *)&naming_addr, sizeof(naming_addr)) < 0)
    {
        perror("Naming bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(naming_server_fd, MAX_CLIENTS) < 0)
    {
        perror("Naming listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Naming server is listening for clients on port %d...\n", NAMING_PORT);

    // Handle client connections
    while (1)
    {
        socklen_t storage_addrlen = sizeof(naming_addr);
        int client_sock = accept(naming_server_fd, (struct sockaddr *)&naming_addr, &storage_addrlen);
        if (client_sock < 0)
            continue;

        struct
        {
            int socket;
            StorageServerTable *table;
        } *args = malloc(sizeof(*args));
        args->socket = client_sock;
        args->table = server_table;

        pthread_t client_thread;
        pthread_create(&client_thread, NULL, clientHandler, args);
        pthread_detach(client_thread);
    }

    // Cleanup
    // freeNode(storage_info.root);
    close(storage_server_fd);
    close(naming_server_fd);
    return 0;
}
