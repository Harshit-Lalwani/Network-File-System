#include "header.h"
#include "lru_cache.h"

LRUCache *cache;
AsyncWriteState *writeStateQueue = NULL; // Head of the queue
pthread_mutex_t queueMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER; // Mutex to protect log file access
pthread_t monitorThread;
// Log file path
const char *log_file_path = "serverlog.txt";

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
    // table->count++;
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
    Node *cachedNode = getLRUCache(cache, path);
    if (cachedNode != NULL)
    {
        // Return the server associated with the cached node
        for (int i = 0; i < TABLE_SIZE; i++)
        {
            pthread_mutex_lock(&table->locks[i]);
            StorageServer *server = table->table[i];
            while (server)
            {
                if (server->root == cachedNode)
                {
                    pthread_mutex_unlock(&table->locks[i]);
                    return server;
                }
                server = server->next;
            }
            pthread_mutex_unlock(&table->locks[i]);
        }
    }

    // If not found in cache, search in the storage servers
    for (int i = 0; i < TABLE_SIZE; i++)
    {
        pthread_mutex_lock(&table->locks[i]);
        StorageServer *server = table->table[i];
        while (server)
        {
            if (server->active && server->root)
            {
                Node *found_node = searchPath(server->root, path);
                if (found_node != NULL)
                {
                    putLRUCache(cache, path, found_node); // Cache the found node
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

StorageServer *findStorageServerByPath2(StorageServerTable *table, const char *path)
{
    // No need for path copy and tokenization since searchPath handles that
    for (int i = 0; i < TABLE_SIZE; i++)
    {
        pthread_mutex_lock(&table->locks[i]);
        StorageServer *server = table->table[i];

        while (server)
        {
            if (!server->active)
            {
                // Use searchPath to check if this server has the path

                if (strcmp(server->root->name, path) == 0)
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
    StorageServer *existing_server = findStorageServerByPath2(table, server->root->name);
    if (existing_server)
    {
        server->id=existing_server->id;
        unsigned int index = hashStorageServer(existing_server->ip, existing_server->nm_port);
        pthread_mutex_lock(&table->locks[index]);
        //add or correct it if already exist then dont increase the count just remove the older one and add new one
        StorageServer *prev = NULL;
        StorageServer *current = table->table[index];
        while (current)
        {
            if (current == existing_server)
            {
                if (prev)
                    prev->next = current->next;
                else
                    table->table[index] = current->next;

                // Free the existing server resources
                close(existing_server->socket);
                pthread_mutex_destroy(&existing_server->lock);
                free(existing_server->root); // Assuming root needs to be freed
                free(existing_server);

                break;
            }
            prev = current;
            current = current->next;
        }
        pthread_mutex_unlock(&table->locks[index]);
        unsigned int index2 = hashStorageServer(server->ip, server->nm_port);
        pthread_mutex_lock(&table->locks[index2]);
        server->next = table->table[index2];
        table->table[index2] = server;
        pthread_mutex_unlock(&table->locks[index2]);
        return server;
    }
    table->count++;
    server->id = table->count;
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
        memset(command, 0, sizeof(command));
        ssize_t bytes_received = recv(server->socket, command, sizeof(command) - 1, 0);

        if (bytes_received <= 0)
        {
            pthread_mutex_lock(&server->lock);
            server->active = false;
            pthread_mutex_unlock(&server->lock);
            printf("Storage server %s disconnected\n", server->ip);
            log_message(server->ip, server->nm_port, "SS", "Storage Server Disconnected.");
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

    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    // Now, let's log the action (get client's IP and port)
    if (getpeername(client_socket, (struct sockaddr *)&client_addr, &addr_len) == -1)
    {
        perror("getpeername failed");
        return NULL;
    }

    // Extract the client IP address and port
    char client_ip[INET_ADDRSTRLEN];
    int client_port;
    get_ip_and_port(&client_addr, client_ip, &client_port);
    while (1)
    {
        memset(buffer, 0, sizeof(buffer));
        bytes_received = recv(client_socket, buffer, sizeof(buffer), 0);
        buffer[bytes_received] = '\0';
        log_message(client_ip, client_port, "Received from Client:", buffer);
        if (sscanf(buffer, "%s %s", command, path) < 1)
        {
            send(client_socket, " \033[1;31mERROR 101:\033[0m \033[38;5;214mInvalid command!\n\0\033[0m",
                 strlen(" \033[1;31mERROR 101:\033[0m \033[38;5;214mInvalid command!\n\0\033[0m"), 0);
            log_message(client_ip, client_port, "Sent to Client:", " \033[1;31mERROR 101:\033[0m \033[38;5;214mInvalid command!\n\0\033[0m");
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
            if (!server || server->active != 1)
            {
                const char *error = " \033[1;31mERROR: 404\033[0m \033[38;5;214mPath not found!\n\0\033[0m";
                send(client_socket, error, strlen(error), 0);
                log_message(client_ip, client_port, "Sent to Client:", error);
                printf("sent error\n");
                fflush(stdout);
                continue;
            }
            printf("%s\n", server->root->name);
            pthread_mutex_lock(&server->lock);
            char response[MAX_BUFFER_SIZE];
            if (server->active)
            {
                printf("storage details %s %d\n", server->ip, server->client_port);
                memset(response, 0, sizeof(response));
                snprintf(response, sizeof(response), "StorageServer: %s : %d", server->ip, server->client_port);
                send(client_socket, response, strlen(response), 0);
                log_message(client_ip, client_port, "Sent to Client(SS Details):", response);
            }
            else
            {
                const char *error = " \033[1;31mERROR 402:\033[0m \033[38;5;214mStorge Server not active.\n\0\033[0m";
                send(client_socket, error, strlen(error), 0);
                log_message(client_ip, client_port, "Sent to Client:", error);
            }
            pthread_mutex_unlock(&server->lock);
        }
        else if (sscanf(buffer, "LIST %s", path) == 1 || strncmp(buffer, "LIST", 4) == 0)
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
                    log_message(client_ip, client_port, "Sent to Client:", response);
                }
                else
                {
                    const char *error = " \033[1;31mERROR 401:\033[0m \033[38;5;214mNo Files or Directories found in the path.\n\0\033[0m";
                    send(client_socket, error, strlen(error), 0);
                    log_message(client_ip, client_port, "Sent to Client:", error);
                }
            }
            else
            {
                StorageServerList *servers = findStorageServersByPath_List(table, path);
                if (!servers)
                {
                    const char *error = " \033[1;31mERROR 404:\033[0m \033[38;5;214mPath not found!\n\0\033[0m";
                    send(client_socket, error, strlen(error), 0);
                    log_message(client_ip, client_port, "Sent to Client:", error);

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
                            const char *error = " \033[1;31mERROR 404:\033[0m \033[38;5;214mPath not found!\n\0\033[0m";
                            send(client_socket, error, strlen(error), 0);
                            log_message(client_ip, client_port, "Sent to Client:", error);
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
                        const char *error = " \033[1;31mERROR 402:\033[0m \033[38;5;214mStorage Server not active.\0\n\033[0m";
                        send(client_socket, error, strlen(error), 0);
                        log_message(client_ip, client_port, "Sent to Client:", error);
                    }
                    pthread_mutex_unlock(&server->lock);
                }

                // Send the response with all the matching servers
                if (response_offset > 0)
                {
                    send(client_socket, response, response_offset, 0); // Send the listing response to the client
                    log_message(client_ip, client_port, "Sent to Client:", response);
                }
                else
                {
                    const char *error = " \033[1;31mERROR 401:\033[0m \033[38;5;214mNo files or directories found in the path.\n\0\033[0m";
                    send(client_socket, error, strlen(error), 0);
                    log_message(client_ip, client_port, "Sent to Client:", error);
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
                        int flag = 0;
                        while (server)
                        {
                            if (server && server->id == ss_num)
                            {
                                // ss_num--;
                                flag = 1;
                                break;
                            }
                            server = server->next;
                        }
                        pthread_mutex_unlock(&table->locks[i]);
                        if (flag)
                            break;
                    }
                    if (server != NULL)
                    {
                        // printf("hii\n");
                        pthread_mutex_lock(&server->lock);
                        if (server->active)
                        {
                            char respond[100001];
                            send(server->socket, buffer, strlen(buffer), 0);
                            // recv(server->socket,buffer,sizeof(buffer),0);
                            // send(server->socket,buffer,strlen(buffer),0);
                            log_message(server->ip, server->nm_port, "Sent to SS:", buffer);
                            memset(respond, 0, sizeof(respond));
                            // printf("hyubnj\n");
                            // usleep(10000);
                            if (recv(server->socket, respond, sizeof(respond), 0) < 0)
                            {
                                perror("receive ns");
                            }
                            // printf(" hjbhjbj\n");
                            log_message(server->ip, server->nm_port, "Received from SS:", respond);
                            fflush(stdout);
                            if (strncmp(respond, "CREATE DONE", 11) == 0)
                            {
                                // printf("yeahhh\n");
                                char *lastSlash = strrchr(path, '/');
                                if (!lastSlash)
                                {
                                    send(client_socket, " \033[1;31mERROR 404:\033[0m \033[38;5;214mPath not found!\033[0m\n\0", strlen(" \033[1;31mERROR 404:\033[0m \033[38;5;214mPath not found!\033[0m\n\0"), 0);
                                    log_message(client_ip, client_port, "Sent to Client:", " \033[1;31mERROR 404:\033[0m \033[38;5;214mPath not found!\033[0m\n\0");
                                    return NULL;
                                }
                                *lastSlash = '\0';
                                char *name = lastSlash + 1;
                                Node *parentDir = searchPath(server->root, path);
                                *lastSlash = '/';
                                if (!parentDir)
                                {
                                    send(client_socket, " \033[1;31mERROR 100:\033[0m \033[38;5;214mParent Directory Missing!\033[0m\n\0", strlen(" \033[1;31mERROR 100:\033[0m \033[38;5;214mParent Directory Missing!\033[0m\n\0"), 0);
                                    log_message(client_ip, client_port, "Sent to Client:", " \033[1;31mERROR 100:\033[0m \033[38;5;214mParent Directory Missing!\033[0m\n\0");

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
                            // printf("bhbbh\n");
                            fflush(stdout);
                            send(client_socket, respond, strlen(respond), 0);
                            // printf("bhbbsgfsgh\n");
                            fflush(stdout);
                            log_message(client_ip, client_port, "Sent to Client:", respond);
                        }
                        else
                        {
                            const char *error = "Storage server is not active";
                            send(client_socket, error, strlen(error), 0);
                            log_message(client_ip, client_port, "Sent to Client:", error);
                        }
                        pthread_mutex_unlock(&server->lock);
                    }
                    else
                    {
                        // printf("bhbh\n");
                        fflush(stdout);
                        const char *error = " \033[1;31mERROR 402:\033[0m \033[38;5;214mStorage Server not active.\033[0m\n\0";
                        send(client_socket, error, strlen(error), 0);
                        log_message(client_ip, client_port, "Sent to Client:", error);
                    }
                }
                else
                {
                    printf("else   \n");
                    fflush(stdout);
                    send(client_socket, " \033[1;31mERROR 101:\033[0m \033[38;5;214mInvalid Command CREATE type!\033[0m\n\0", strlen(" \033[1;31mERROR 101:\033[0m \033[38;5;214mInvalid Command CREATE type!\033[0m\n\0"), 0);
                    log_message(client_ip, client_port, "Sent to Client:", " \033[1;31mERROR 101:\033[0m \033[38;5;214mInvalid Command CREATE type!\033[0m\n\0");
                }
                // }
            }
            else if (sscanf(buffer, "DELETE %s", path) == 1)
            {
                StorageServer *server = findStorageServerByPath(table, path);
                if (!server)
                {
                    const char *error = " \033[1;31mERROR 404:\033[0m \033[38;5;214mPath not found!\033[0m\n\0";
                    send(client_socket, error, strlen(error), 0);
                    log_message(client_ip, client_port, "Sent to Client:", error);

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
                        log_message(server->ip, server->nm_port, "Sent to SS:", buffer);

                        memset(respond, 0, sizeof(respond));
                        recv(server->socket, respond, sizeof(respond), 0);
                        log_message(server->ip, server->nm_port, "Received from SS:", respond);

                        printf("%s\n", respond);
                        if (strcmp(respond, "DELETE DONE") == 0)
                        {
                            Node *nodeToDelete = searchPath(server->root, path);
                            deleteNode(nodeToDelete);
                            putLRUCache(cache, path, NULL);
                        }
                        send(client_socket, respond, strlen(respond), 0);
                        log_message(client_ip, client_port, "Sent to Client:", respond);
                    }
                    else
                    {
                        const char *error = " \033[1;31mERROR 402:\033[0m \033[38;5;214mStorage Server not active.\033[0m\n\0";
                        send(client_socket, error, strlen(error), 0);
                        log_message(client_ip, client_port, "Sent to Client:", error);
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
                    const char *error = " \033[1;31mERROR 404:\033[0m \033[38;5;214mSource Path not found!\033[0m\n\0";
                    send(client_socket, error, strlen(error), 0);
                    log_message(client_ip, client_port, "Sent to Client:", error);

                    continue;
                }
                Node *source_node = findNode(source_server->root, path);
                if (!source_node)
                {
                    const char *error = " \033[1;31mERROR 404:\033[0m \033[38;5;214mSource Path not found!\033[0m\n\0";
                    send(client_socket, error, strlen(error), 0);
                    log_message(client_ip, client_port, "Sent to Client:", error);
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
                        const char *error = " \033[1;31mERROR 404:\033[0m \033[38;5;214mDestination Path not found!\033[0m\n\0";
                        send(client_socket, error, strlen(error), 0);
                        log_message(client_ip, client_port, "Sent to Client:", error);
                        continue;
                    }

                    // Check if parent directory exists and is actually a directory
                    Node *parent_node = findNode(dest_server->root, parent_path);
                    if (!parent_node || parent_node->type != DIRECTORY_NODE)
                    {
                        const char *error = " \033[1;31mERROR 400:\033[0m \033[38;5;214mPath is not a directory!\033[0m\n\0";
                        send(client_socket, error, strlen(error), 0);
                        log_message(client_ip, client_port, "Sent to Client:", error);
                        continue;
                    }
                    else
                    {
                        char init_cmd[MAX_BUFFER_SIZE];
                        memset(buffer, 0, sizeof(buffer));
                        snprintf(buffer, sizeof(buffer), "COPY %s %s", path, parent_path);
                        send(source_server->socket, buffer, strlen(buffer), 0);
                        log_message(source_server->ip, source_server->nm_port, "Sent to SS:", buffer);
                        memset(init_cmd, 0, sizeof(init_cmd));
                        recv(source_server->socket, init_cmd, sizeof(init_cmd), 0);
                        log_message(source_server->ip, source_server->nm_port, "Received from SS:", init_cmd);

                        char server_info[MAX_BUFFER_SIZE];
                        memset(server_info, 0, sizeof(server_info));
                        snprintf(server_info, sizeof(server_info), "SOURCE SERVER_INFO %s %d",
                                 dest_server->ip, dest_server->client_port);
                        send(source_server->socket, server_info, strlen(server_info), 0);
                        log_message(source_server->ip, source_server->nm_port, "Sent to SS:", server_info);

                        char response[100001];
                        memset(response, 0, sizeof(response));
                        recv(source_server->socket, response, sizeof(response), 0);
                        log_message(source_server->ip, source_server->nm_port, "Received from SS:", response);

                        if (strncmp(response, "COPY DONE", 9) == 0)
                        {
                            // implement logic to copy files and directrix from the source hast table to dest in the directrix we are copying the data
                            if (source_node->type == DIRECTORY_NODE)
                            {
                                Node *destParentNode = findNode(dest_server->root, parent_path);
                                if (!destParentNode || destParentNode->type != DIRECTORY_NODE)
                                {
                                    printf("Error: Destination path is not a valid directory\n");
                                    const char *error = " \033[1;31mERROR 400:\033[0m \033[38;5;214mDestination Path is not a directory!\033[0m\n\0";
                                    send(client_socket, error, strlen(error), 0);
                                    log_message(client_ip, client_port, "Sent to Client:", error);
                                    continue;
                                }
                                addDirectory(destParentNode, source_node->name, source_node->permissions);
                                Node *newRootDir = searchNode(destParentNode->children, source_node->name);
                                // Copy the contents of the source directory to the destination directory
                                copyDirectoryContents(source_node, destParentNode);

                                const char *success = "Directory copied successfully";
                                send(client_socket, success, strlen(success), 0);
                                log_message(client_ip, client_port, "Sent to Client:", success);
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
                                log_message(client_ip, client_port, "Sent to Client:", success);
                            }
                        }
                        send(client_socket, response, strlen(response), 0);
                        log_message(client_ip, client_port, "Sent to Client:", response);
                    }
                }
                else
                {
                    Node *dest_node = findNode(dest_server->root, dest_path);
                    if (dest_node->type == FILE_NODE)
                    {
                        const char *error = " \033[1;31mERROR 400:\033[0m \033[38;5;214mDestination Path is not a directory!\033[0m\n\0";
                        send(client_socket, error, strlen(error), 0);
                        log_message(client_ip, client_port, "Sent to Client:", error);

                        continue;
                    }
                    else
                    {
                        printf("hello\n");
                        char init_cmd[MAX_BUFFER_SIZE];
                        send(source_server->socket, buffer, strlen(buffer), 0);
                        log_message(source_server->ip, source_server->nm_port, "Sent to SS:", buffer);
                        memset(init_cmd, 0, sizeof(init_cmd));
                        recv(source_server->socket, init_cmd, sizeof(init_cmd), 0);
                        log_message(source_server->ip, source_server->nm_port, "Received from SS:", init_cmd);
                        char server_info[MAX_BUFFER_SIZE];
                        memset(server_info, 0, sizeof(server_info));
                        snprintf(server_info, sizeof(server_info), "SOURCE SERVER_INFO %s %d",
                                 dest_server->ip, dest_server->client_port);
                        send(source_server->socket, server_info, strlen(server_info), 0);
                        log_message(source_server->ip, source_server->nm_port, "Sent to SS:", server_info);

                        char response[100001];
                        memset(response, 0, sizeof(response));
                        recv(source_server->socket, response, sizeof(response), 0);
                        log_message(source_server->ip, source_server->nm_port, "Received from SS:", response);

                        if (strncmp(response, "COPY DONE", 9) == 0)
                        {
                            // implement logic to copy files and directrix from the source hast table to dest in the directrix we are copying the data
                            if (source_node->type == DIRECTORY_NODE)
                            {
                                printf("%s\n", dest_path);
                                Node *destParentNode = findNode(dest_server->root, dest_path);
                                if (!destParentNode || destParentNode->type != DIRECTORY_NODE)
                                {
                                    printf("Error: Destination path is not a valid directory\n");
                                    const char *error = " \033[1;31mERROR 400:\033[0m \033[38;5;214mDestination Path is not a valid Directory!\033[0m\n\0";
                                    send(client_socket, error, strlen(error), 0);
                                    log_message(client_ip, client_port, "Sent to Client:", error);
                                    continue;
                                }
                                addDirectory(destParentNode, source_node->name, source_node->permissions);
                                Node *newRootDir = searchNode(destParentNode->children, source_node->name);
                                // Copy the contents of the source directory to the destination directory
                                copyDirectoryContents(source_node, newRootDir);

                                const char *success = "Directory copied successfully";
                                send(client_socket, success, strlen(success), 0);
                                log_message(client_ip, client_port, "Sent to Client:", success);
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
                                log_message(client_ip, client_port, "Sent to Client:", success);
                            }
                        }
                        send(client_socket, response, strlen(response), 0);
                        log_message(client_ip, client_port, "Received from Client:", response);
                    }
                }
            }
            else
            {
                send(client_socket, " \033[1;31mERROR 101:\033[0m \033[38;5;214mInvalid Command!\033[0m\n\0",
                     strlen(" \033[1;31mERROR 101:\033[0m \033[38;5;214mInvalid Command!\033[0m\n\0"), 0);
                log_message(client_ip, client_port, "Sent to Client:", " \033[1;31mERROR 101:\033[0m \033[38;5;214mInvalid Command!\033[0m\n\0");
            }
        }
        else if (strcmp(command, "EXIT") == 0)
        {
            break;
        }
        else
        {
            send(client_socket, " \033[1;31mERROR 101:\033[0m \033[38;5;214mInvalid Command!\033[0m\n\0",
                 strlen(" \033[1;31mERROR 101:\033[0m \033[38;5;214mInvalid Command!\033[0m\n\0"), 0);
            log_message(client_ip, client_port, "Sent to Client:", " \033[1;31mERROR 101:\033[0m \033[38;5;214mInvalid Command!\033[0m\n\0");
        }
    }
}

// Function to receive all server information
int receiveServerInfo(int sock, char *ip_out, int *nm_port_out, int *client_port_out, Node **root_out)
{
    char buffer[1024];
    int bytes_received;
    memset(buffer, 0, sizeof(buffer));
    bytes_received = recv(sock, buffer, sizeof(buffer), 0);
    if (bytes_received <= 0)
    {
        perror("Failed to receive data");
        return -1;
    }
    struct sockaddr_in ss_addr;
    socklen_t addr_len = sizeof(ss_addr);

    // Now, let's log the action (get client's IP and port)
    if (getpeername(sock, (struct sockaddr *)&ss_addr, &addr_len) == -1)
    {
        perror("getpeername failed");
        return -1;
    }

    // Extract the client IP address and port
    char ss_ip[INET_ADDRSTRLEN];
    int ss_port;
    get_ip_and_port(&ss_addr, ss_ip, &ss_port);

    // Log the message to the log file
    log_message(ss_ip, ss_port, "Received from SS:", buffer);
    // memset(buffer, 0 , sizeof(buffer));
    memcpy(ip_out, buffer, 16);
    memcpy(nm_port_out, buffer + 16, sizeof(int));
    memcpy(client_port_out, buffer + 16 + sizeof(int), sizeof(int));
    ip_out[15] = '\0';
    send(sock, buffer, sizeof(buffer), 0);
    log_message(ss_ip, ss_port, "Sent to SS:", buffer);

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
        log_message(NULL, 0, "SS", "New Storage Server Connected!");

        // Handle the new storage server connection in a separate thread
        StorageServer *server = handleNewStorageServer(storage_sock, server_table);
        if (!server)
        {
            printf("Failed to handle new storage server connection.\n");
            log_message(NULL, 0, "SS", "Failed to handle new storage server connection.");

            close(storage_sock);
            continue;
        }

        // Create a new thread to handle this storage server
        pthread_t server_thread;
        if (pthread_create(&server_thread, NULL, storageServerHandler, server) != 0)
        {
            perror("Failed to create storage server handler thread");
            log_message(NULL, 0, "SS", "Failed to create storage server handler thread");

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
        char log_buf[512]; // Buffer to store the formatted log message

        // Format the message and store it in the log_message buffer
        snprintf(log_buf, sizeof(log_buf),
                 "Storage server successfully registered:\n"
                 "IP: %s\n"
                 "Naming Port: %d\n"
                 "Client Port: %d\n"
                 "Root directory: %s\n"
                 "Total storage servers connected: %d\n",
                 server->ip, server->nm_port, server->client_port, (server->root != NULL) ? server->root->name : "No root directory", server_table->count);
        log_message(NULL, 0, "SS", log_buf);
    }

    return NULL;
}

void get_local_ip(char *ip_buffer, size_t buffer_size)
{
    struct ifaddrs *ifaddr, *ifa;
    int family;

    if (getifaddrs(&ifaddr) == -1)
    {
        perror("getifaddrs");
        exit(EXIT_FAILURE);
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_addr == NULL)
            continue;

        family = ifa->ifa_addr->sa_family;
        if (family == AF_INET)
        { // IPv4 address
            if (strcmp(ifa->ifa_name, "lo") != 0)
            { // Exclude loopback interface
                inet_ntop(AF_INET, &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr, ip_buffer, buffer_size);
                freeifaddrs(ifaddr);
                return;
            }
        }
    }
    freeifaddrs(ifaddr);
    strncpy(ip_buffer, "Unknown", buffer_size);
}

pthread_t ackListenerThread;

int main()
{
    StorageServerTable *server_table = createStorageServerTable();
    cache = createLRUCache(5);
    int storage_server_fd, naming_server_fd;
    struct sockaddr_in storage_addr, naming_addr;
    int opt = 1;

    // Initialize storage server socket
    if ((storage_server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("Storage socket creation failed");
        log_message(NULL, 0, "SS", "Storage socket creation failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(storage_server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
    {
        perror("Set socket options failed");
        log_message(NULL, 0, "SS", "Set socket options failed");
        exit(EXIT_FAILURE);
    }

    storage_addr.sin_family = AF_INET;
    storage_addr.sin_addr.s_addr = INADDR_ANY;
    storage_addr.sin_port = 0;

    if (bind(storage_server_fd, (struct sockaddr *)&storage_addr, sizeof(storage_addr)) < 0)
    {
        perror("Storage bind failed");
        log_message(NULL, 0, "SS", "Storage bind failed");
        exit(EXIT_FAILURE);
    }

    socklen_t addr_len = sizeof(storage_addr);
    if (getsockname(storage_server_fd, (struct sockaddr *)&storage_addr, &addr_len) < 0)
    {
        perror("Getsockname for storage server failed");
        log_message(NULL, 0, "SS", "Getsockname for storage server failed");
        exit(EXIT_FAILURE);
    }

    if (listen(storage_server_fd, 10) < 0)
    {
        perror("Storage listen failed");
        log_message(NULL, 0, "SS", "Storage listen failed");
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
        log_message(NULL, 0, "SS", "Failed to create storage server acceptor thread");

        free(args);
        close(storage_server_fd);
        return -1;
    }

    // Detach the acceptor thread
    pthread_detach(storage_acceptor_thread);

    if (pthread_create(&monitorThread, NULL, monitorWriteStates, NULL) != 0)
    {
        perror("Failed to create monitor thread");
        exit(EXIT_FAILURE);
    }
    pthread_detach(monitorThread);

    if (pthread_create(&ackListenerThread, NULL, ackListener, NULL) != 0)
    {
        perror("Failed to create acknowledgment listener thread");
        log_message(NULL, 0, "SS", "Failed to create acknowledgment listener thread");

        exit(EXIT_FAILURE);
    }

    printf("Acknowledgment listener thread started.\n");
    log_message(NULL, 0, "SS", "Acknowledgment listener thread started.");

    // Detach the thread to allow independent execution
    pthread_detach(ackListenerThread);

    // printf("Storage server acceptor started on port %d\n", STORAGE_PORT);
    // log_message(NULL, 0, "SS", "Storage server acceptor started on STORAGE_PORT");

    if ((naming_server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("Naming socket creation failed");
        log_message(NULL, 0, "NM", "Naming socket creation failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(naming_server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
    {
        perror("Set naming socket options failed");
        log_message(NULL, 0, "NM", "Set naming socket options failed");
        exit(EXIT_FAILURE);
    }

    naming_addr.sin_family = AF_INET;
    naming_addr.sin_addr.s_addr = INADDR_ANY;
    naming_addr.sin_port = 0;

    if (bind(naming_server_fd, (struct sockaddr *)&naming_addr, sizeof(naming_addr)) < 0)
    {
        perror("Naming bind failed");
        log_message(NULL, 0, "NM", "Naming bind failed");
        exit(EXIT_FAILURE);
    }

    addr_len = sizeof(naming_addr);
    if (getsockname(naming_server_fd, (struct sockaddr *)&naming_addr, &addr_len) < 0)
    {
        perror("Getsockname for naming server failed");
        log_message(NULL, 0, "NM", "Getsockname for naming server failed");
        exit(EXIT_FAILURE);
    }

    if (listen(naming_server_fd, MAX_CLIENTS) < 0)
    {
        perror("Naming listen failed");
        log_message(NULL, 0, "NM", "Naming listen failed");
        exit(EXIT_FAILURE);
    }
    char ip_buffer[INET_ADDRSTRLEN];
    get_local_ip(ip_buffer, sizeof(ip_buffer));
    int Storage_port = ntohs(storage_addr.sin_port);
    // inet_ntop(AF_INET, &(naming_addr.sin_addr), ip_buffer, INET_ADDRSTRLEN);
    int naming_port=ntohs(naming_addr.sin_port);
    printf("IP: %s \nStorage_port :%d\nnaming_port :%d\n",ip_buffer,Storage_port,naming_port);
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
    freeLRUCache(cache);
    close(storage_server_fd);
    close(naming_server_fd);
    return 0;
}
