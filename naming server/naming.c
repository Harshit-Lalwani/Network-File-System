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

// Structure to store storage server information
typedef struct
{
    char ip[16];
    int nm_port;
    int client_port;
    Node *root;
} StorageServerInfo;

StorageServerInfo storage_info;

// Function to handle client requests
void handle_client_request(int client_socket, const StorageServerInfo *storage_info, int storage_socket)
{
    char buffer[MAX_BUFFER_SIZE];
    char command[20];
    char path[MAX_PATH_LENGTH];
    char dest_path[MAX_PATH_LENGTH];
    ssize_t bytes_received;

    while ((bytes_received = recv(client_socket, buffer, sizeof(buffer), 0)) > 0)
    {
        // printf("krish\n\n\n\n\n");
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
        // printf("%s \n",path);
        // Check if path exists in the storage server's root

        if (strcmp(command, "READ") == 0 ||
            strcmp(command, "WRITE") == 0 ||
            strcmp(command, "META") == 0 ||
            strcmp(command, "STREAM") == 0)
        {

            Node *node = searchPath(storage_info->root, path);
            if (!node)
            {
                send(client_socket, "Error: Path not found\n",
                     strlen("Error: Path not found\n"), 0);
                continue;
            }

            // Send storage server information to client
            char response[MAX_BUFFER_SIZE];
            snprintf(response, sizeof(response), "StorageServer: %s : %d",
                     storage_info->ip, storage_info->client_port);
            send(client_socket, response, strlen(response), 0);
        }
        else if (strcmp(command, "CREATE") == 0 ||
                 strcmp(command, "DELETE") == 0 ||
                 strcmp(command, "COPY") == 0)
        {
            // send(client_socket,command,sizeof(command),0);
            char type[5];
            if (sscanf(buffer, "CREATE %s %s", type, path) == 2)
            {
                // Handle CREATE FILE or CREATE DIR
                if (strcmp(type, "FILE") == 0 || strcmp(type, "DIR") == 0)
                {
                    printf("hii\n");
                    char respond[100001];
                    send(storage_socket, buffer, strlen(buffer), 0);
                    recv(storage_socket, respond, sizeof(respond), 0);
                    printf("%s\n", respond);
                    if (strncmp(respond, "CREATE DONE",12) == 0)
                    {
                        // printf("yeahhh\n\n\n\n\n");
                        char *lastSlash = strrchr(path, '/');
                        if (!lastSlash)
                        {
                            send(client_socket, "Error: Invalid path format", strlen("Error: Invalid path format"), 0);
                            return;
                        }
                        *lastSlash = '\0';
                        char *name = lastSlash + 1;
                        Node *parentDir = searchPath(storage_info->root, path);
                        *lastSlash = '/';
                        if (!parentDir)
                        {
                            send(client_socket, "Error: Parent directory not found", strlen("Error: Parent directory not found"), 0);
                            return;
                        }
                        NodeType typ;
                        if (strcmp(type, "DIR")==0)
                        {
                            typ=DIRECTORY_NODE;
                        }
                        else
                        {
                            typ=FILE_NODE;
                        }
                        Node *newNode = createNode(name, typ, READ | WRITE, path);
                        newNode->parent = parentDir;
                        insertNode(parentDir->children, newNode);
                    }
                    send(client_socket, respond, strlen(respond), 0);
                    // recv(client_socket,respond,sizeof(respond),0);
                }
                else
                {
                    send(client_socket, "Error: Invalid CREATE type\n", strlen("Error: Invalid CREATE type\n"), 0);
                }
            }
            else if (sscanf(buffer, "DELETE %s", path) == 1)
            {

                char respond[100001];
                send(storage_socket, buffer, sizeof(buffer), 0);
                recv(storage_socket, respond, sizeof(respond), 0);
                if (strcmp(respond, "DELETE DONE") == 0)
                {
                    Node *nodeToDelete = searchPath(storage_info->root, path);
                    deleteNode(nodeToDelete);
                }
                send(client_socket, respond, sizeof(respond), 0);
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

int main()
{
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

    if (listen(storage_server_fd, 1) < 0)
    {
        perror("Storage listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Waiting for storage server connection on port %d...\n", STORAGE_PORT);

    // Accept storage server connection
    int storage_sock;
    socklen_t storage_addrlen = sizeof(storage_addr);
    if ((storage_sock = accept(storage_server_fd, (struct sockaddr *)&storage_addr,
                               &storage_addrlen)) < 0)
    {
        perror("Storage accept failed");
        exit(EXIT_FAILURE);
    }

    printf("Storage server connected.\n");

    // Receive storage server information
    if (receiveServerInfo(storage_sock, storage_info.ip, &storage_info.nm_port,
                          &storage_info.client_port, &storage_info.root) != 0)
    {
        printf("Failed to receive storage server information.\n");
        exit(EXIT_FAILURE);
    }

    printf("Received storage server information:\n");
    printf("IP: %s\n", storage_info.ip);
    printf("Naming Port: %d\n", storage_info.nm_port);
    printf("Client Port: %d\n", storage_info.client_port);
    printf("client root %s\n",storage_info.root->name);

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

    if (bind(naming_server_fd, (struct sockaddr *)&naming_addr,
             sizeof(naming_addr)) < 0)
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
        int client_sock;
        struct sockaddr_in client_addr;
        socklen_t client_addrlen = sizeof(client_addr);

        if ((client_sock = accept(naming_server_fd, (struct sockaddr *)&client_addr,
                                  &client_addrlen)) < 0)
        {
            perror("Client accept failed");
            continue;
        }

        printf("New client connected from %s:%d\n",
               inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        // Handle client requests
        handle_client_request(client_sock, &storage_info, storage_sock);
        close(client_sock);
    }

    // Cleanup
    freeNode(storage_info.root);
    close(storage_server_fd);
    close(naming_server_fd);
    return 0;
}