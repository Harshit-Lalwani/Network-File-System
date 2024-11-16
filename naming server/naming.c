#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <ctype.h>
#include "header.h"

#define MAX_CLIENTS 10
#define STORAGE_PORT 8080
#define NAMING_PORT 8081
// #define CLIENT_PORT 8082
#define MAX_BUFFER_SIZE 100001

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

// Function to handle client requests
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
        bytes_received = recv(client_socket, buffer, sizeof(buffer), 0);
        buffer[bytes_received] = '\0';
        // Parse command
        if (sscanf(buffer, "%s %s", command, path) < 1)
        {
            send(client_socket, "Error: Invalid command format\n",
                 strlen("Error: Invalid command format\n"), 0);
            continue;
        }

        // Convert command to uppercase for comparison
        for (int i = 0; command[i]; i++)
        {
            command[i] = toupper(command[i]);
        }
        printf("%s \n", path);
        // Check if path exists in the storage server's root

        if (strcmp(command, "READ") == 0 || strcmp(command, "WRITE") == 0 || strcmp(command, "META") == 0 || strcmp(command, "STREAM") == 0)
        {
            StorageServer *server = findStorageServerByPath(table, path);

            if (!server)
            {
                const char *error = "Path not found in any storage server";
                send(client_socket, error, strlen(error), 0);
                continue;
            }
            pthread_mutex_lock(&server->lock);
            char response[MAX_BUFFER_SIZE];
            if (server->active)
            {
                printf("storage details %s %d\n", server->ip, server->client_port);
                snprintf(response, sizeof(response), "StorageServer: %s : %d", server->ip, server->client_port);
                send(client_socket, response, strlen(response), 0);
            }
            else
            {
                const char *error = "Storage server is not active";
                send(client_socket, error, strlen(error), 0);
            }
            pthread_mutex_unlock(&server->lock);
            // snprintf(response, sizeof(response), "StorageServer: %s : %d",
            //          storage_info->ip, storage_info->client_port);
            // send(client_socket, response, strlen(response), 0);
        }
        else if (strcmp(command, "CREATE") == 0 || strcmp(command, "DELETE") == 0 || strcmp(command, "COPY") == 0)
        {
            // send(client_socket,command,sizeof(command),0);
            char type[5];
            int ss_num = -1;
            if (sscanf(buffer, "CREATE %s %d %s", type, &ss_num, path) == 3)
            {
                // StorageServer *server = findStorageServerByPath(table, path);
                // if (!server)
                // {
                //     const char *error = "Path not found in any storage server";
                //     send(client_socket, error, strlen(error), 0);
                //     continue;
                // }
                // else
                // {
                if (strcmp(type, "FILE") == 0 || strcmp(type, "DIR") == 0)
                {
                    printf("%d \n",ss_num);
                    StorageServer *server;
                    for (int i = 0; i < TABLE_SIZE && ss_num!=0; i++)
                    {
                        pthread_mutex_lock(&table->locks[i]);
                        server = table->table[i];
                        if(server)
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
                            recv(server->socket, respond, sizeof(respond), 0);
                            printf("%s\n", respond);
                            if (strncmp(respond, "CREATE DONE", 11) == 0)
                            {
                                printf("yeahhh\n");
                                char *lastSlash = strrchr(path, '/');
                                if (!lastSlash)
                                {
                                    send(client_socket, "Error: Invalid path format", strlen("Error: Invalid path format"), 0);
                                    return;
                                }
                                *lastSlash = '\0';
                                char *name = lastSlash + 1;
                                Node *parentDir = searchPath(server->root, path);
                                *lastSlash = '/';
                                if (!parentDir)
                                {
                                    send(client_socket, "Error: Parent directory not found", strlen("Error: Parent directory not found"), 0);
                                    return;
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
                        printf("server root %s\n",server->root->name);
                        send(server->socket, buffer, strlen(buffer), 0);
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
                // Handle COPY
                // if (send_copy_request(storage_info, path, dest_path) == 0)
                // {
                //     send(client_socket, "COPY: Success\n", strlen("COPY: Success\n"), 0);
                // }
                // else
                // {
                //     send(client_socket, "COPY: Failed\n", strlen("COPY: Failed\n"), 0);
                // }
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

Node *receiveNodeChain(int sock)
{
    Node *head = NULL;
    Node *current = NULL;

    while (1)
    {
        // Check for end of chain
        int marker;
        if (recv(sock, &marker, sizeof(int), 0) <= 0)
            return NULL;
        send(sock, "OK", 2, 0);
        if (marker == -1)
            break; // End of chain

        // Receive node data
        int name_len;
        if (recv(sock, &name_len, sizeof(int), 0) <= 0)
            return NULL;
        send(sock, "OK", 2, 0);

        char *name = malloc(name_len);
        if (recv(sock, name, name_len, 0) <= 0)
        {
            free(name);
            return NULL;
        }
        send(sock, "OK", 2, 0);

        NodeType type;
        Permissions permissions;
        if (recv(sock, &type, sizeof(NodeType), 0) <= 0)
        {
            free(name);
            return NULL;
        }
        send(sock, "OK", 2, 0);

        if (recv(sock, &permissions, sizeof(Permissions), 0) <= 0)
        {
            free(name);
            return NULL;
        }
        send(sock, "OK", 2, 0);

        int loc_len;
        if (recv(sock, &loc_len, sizeof(int), 0) <= 0)
        {
            free(name);
            return NULL;
        }
        send(sock, "OK", 2, 0);

        char *dataLocation = malloc(loc_len);
        if (recv(sock, dataLocation, loc_len, 0) <= 0)
        {
            free(name);
            free(dataLocation);
            return NULL;
        }
        send(sock, "OK", 2, 0);

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
        send(sock, "OK", 2, 0);

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

// Function to receive all server information
int receiveServerInfo(int sock, char *ip_out, int *nm_port_out, int *client_port_out, Node **root_out)
{
    char buffer[1024];
    int bytes_received;
    bytes_received = recv(sock, buffer, sizeof(buffer), 0);
    if (bytes_received <= 0)
    {
        perror("Failed to receive data");
        return -1;
    }
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

    printf("Storage server acceptor started on port %d\n", STORAGE_PORT);
    // printf("Waiting for storage server connection on port %d...\n", STORAGE_PORT);

    // // Accept storage server connection
    // int storage_sock;
    // socklen_t storage_addrlen = sizeof(storage_addr);
    // if ((storage_sock = accept(storage_server_fd, (struct sockaddr *)&storage_addr,
    //                            &storage_addrlen)) < 0)
    // {
    //     perror("Storage accept failed");
    //     exit(EXIT_FAILURE);
    // }

    // printf("Storage server connected.\n");

    // // Receive storage server information
    // if (receiveServerInfo(storage_sock, storage_info.ip, &storage_info.nm_port,
    //                       &storage_info.client_port, &storage_info.root) != 0)
    // {
    //     printf("Failed to receive storage server information.\n");
    //     exit(EXIT_FAILURE);
    // }

    // printf("Received storage server information:\n");
    // printf("IP: %s\n", storage_info.ip);
    // printf("Naming Port: %d\n", storage_info.nm_port);
    // printf("Client Port: %d\n", storage_info.client_port);
    // printf("client root %s\n", storage_info.root->name);

    // Initialize naming server socket for client connections
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
