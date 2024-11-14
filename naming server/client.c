#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <asm-generic/socket.h>
#include"header.h"

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
        if (marker == -1)
            break; // End of chain

        // Receive node data
        int name_len;
        if (recv(sock, &name_len, sizeof(int), 0) <= 0)
            return NULL;
        char *name = malloc(name_len);
        if (recv(sock, name, name_len, 0) <= 0)
        {
            free(name);
            return NULL;
        }

        NodeType type;
        Permissions permissions;
        if (recv(sock, &type, sizeof(NodeType), 0) <= 0)
        {
            free(name);
            return NULL;
        }
        if (recv(sock, &permissions, sizeof(Permissions), 0) <= 0)
        {
            free(name);
            return NULL;
        }

        int loc_len;
        if (recv(sock, &loc_len, sizeof(int), 0) <= 0)
        {
            free(name);
            return NULL;
        }
        char *dataLocation = malloc(loc_len);
        if (recv(sock, dataLocation, loc_len, 0) <= 0)
        {
            free(name);
            free(dataLocation);
            return NULL;
        }

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
    // Receive initialization message
    char init_msg;
    if (recv(sock, &init_msg, sizeof(char), 0) <= 0)
        return -1;
    if (init_msg != 'I')
        return -1;

    // Receive server details
    if (recv(sock, ip_out, 16, 0) <= 0)
        return -1;
    if (recv(sock, nm_port_out, sizeof(int), 0) <= 0)
        return -1;
    if (recv(sock, client_port_out, sizeof(int), 0) <= 0)
        return -1;

    // Receive the root node and its entire structure
    *root_out = receiveNodeChain(sock);
    if (*root_out == NULL)
        return -1;

    return 0;
}

int main()
{
    int server_fd, client_sock;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
    {
        perror("Set socket options failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("Bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 5) < 0)
    {
        perror("Listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Storage server is listening on port %d...\n", 8080);

    // while (1)
    // {
        if ((client_sock = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0)
        {
            perror("Accept failed");
            // continue;
        }

        printf("Client connected.\n");

        char ip[16];
        int nm_port, client_port;
        Node *root = NULL;

        if (receiveServerInfo(client_sock, ip, &nm_port, &client_port, &root) == 0)
        {
            printf("Received information from client:\n");
            printf("IP Address: %s\n", ip);
            printf("Naming Server Port: %d\n", nm_port);
            printf("Client Port: %d\n", client_port);
        }
        else
        {
            printf("Failed to receive server information.\n");
        }
        
        

        freeNode(root);
        close(client_sock);
    // }

    close(server_fd);
    return 0;
}
