#include "header.h"
#define PORT 8080

int sendNodeChain(int sock, Node *node)
{
    Node *current = node;
    char ack[1024];
    while (current != NULL)
    {
        // Send marker for valid node
        int valid_marker = 1;
        if (send(sock, &valid_marker, sizeof(int), 0) < 0)
            return -1;
        recv(sock,ack,sizeof(ack),0);

        // Send node data
        int name_len = strlen(current->name) + 1;
        if (send(sock, &name_len, sizeof(int), 0) < 0)
            return -1;
        recv(sock, ack, sizeof(ack), 0);

        if (send(sock, current->name, name_len, 0) < 0)
            return -1;
        recv(sock, ack, sizeof(ack), 0);

        if (send(sock, &current->type, sizeof(NodeType), 0) < 0)
            return -1;
        recv(sock, ack, sizeof(ack), 0);

        if (send(sock, &current->permissions, sizeof(Permissions), 0) < 0)
            return -1;
        recv(sock, ack, sizeof(ack), 0);

        int loc_len = strlen(current->dataLocation) + 1;
        if (send(sock, &loc_len, sizeof(int), 0) < 0)
            return -1;
        recv(sock, ack, sizeof(ack), 0);

        if (send(sock, current->dataLocation, loc_len, 0) < 0)
            return -1;
        recv(sock, ack, sizeof(ack), 0);

        // If this node has children (is a directory)
        if (current->type == DIRECTORY_NODE && current->children != NULL)
        {
            // Send marker indicating has children
            int has_children = 1;
            if (send(sock, &has_children, sizeof(int), 0) < 0)
                return -1;
            recv(sock, ack, sizeof(ack), 0);

            // Send the entire hash table of children
            for (int i = 0; i < TABLE_SIZE; i++)
            {
                if (sendNodeChain(sock, current->children->table[i]) < 0)
                    return -1;
            }
        }
        else
        {
            // Send marker indicating no children
            int has_children = 0;
            if (send(sock, &has_children, sizeof(int), 0) < 0)
                return -1;
            recv(sock, ack, sizeof(ack), 0);
        }

        current = current->next;
    }
    // sleep(1);
    // Send end of chain marker
    int end_marker = -1;
    if (send(sock, &end_marker, sizeof(int), 0) < 0)
        return -1;
    recv(sock, ack, sizeof(ack), 0);

    return 0;
}

// Function to send server information including the hash table
int sendServerInfo(int sock, const char *ip, int nm_port, int client_port, Node *root)
{
    char buffer[1024]; // Adjust the size as needed
    int offset = 0;

    // Copy IP address to buffer
    memcpy(buffer + offset, ip, 16);
    offset += 16;

    // Copy NM port to buffer
    memcpy(buffer + offset, &nm_port, sizeof(int));
    offset += sizeof(int);

    // Copy client port to buffer
    memcpy(buffer + offset, &client_port, sizeof(int));
    offset += sizeof(int);

    // Send the entire buffer
    if (send(sock, buffer, offset, 0) < 0)
    {
        perror("Failed to send data");
        return -1;
    }
    recv(sock, buffer, sizeof(buffer), 0);
    // Send the root node and its entire structure
    return sendNodeChain(sock, root);
}

void *handleClient(void *arg)
{
    struct ClientData *data = (struct ClientData *)arg;
    int client_socket = data->socket;
    Node *root = data->root;
    char buffer[1024];

    while (1)
    {
        memset(buffer, 0, sizeof(buffer));
        ssize_t bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        // send(client_socket, "Received\n", strlen("Received\n"), 0);
        if (bytes_received <= 0)
        {
            printf("Client disconnected\n");
            break;
        }

        // Check if client wants to exit
        if (strncasecmp(buffer, "exit", 4) == 0)
        {
            printf("Client requested to exit\n");
            break;
        }
        processCommand_user(data->root,buffer,data->socket);
        // Placeholder response
        // const char *response = "Request received and processed\n";
        // send(client_socket, response, strlen(response), 0);
    }

    close(client_socket);
    free(data);
    pthread_exit(NULL);
}

// Main function
int main()
{
    // Create root directory
    Node *root = createNode("home", DIRECTORY_NODE, READ | WRITE | EXECUTE, "/home");

    // Start traversal from the root directory
    traverseAndAdd(root, "/home");
    int naming_server_sock;
    struct sockaddr_in naming_naming_serv_addr;
    struct sockaddr_in naming_serv_addr;

    // Creating a socket
    naming_server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (naming_server_sock < 0)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Defining server address
    naming_serv_addr.sin_family = AF_INET;
    naming_serv_addr.sin_port = htons(8080);

    if (inet_pton(AF_INET, "127.0.0.1", &naming_serv_addr.sin_addr) <= 0)
    {
        perror("Invalid address or address not supported");
        exit(EXIT_FAILURE);
    }

    // Connecting to the server
    while (1)
    {
        if (connect(naming_server_sock, (struct sockaddr *)&naming_serv_addr, sizeof(naming_serv_addr)) < 0)
        {
            perror("Connection failed");
            return 0;
        }
        else
        {
            printf("Connected to Storage server. \n");
            break;
        }
    }

    // Prepare and send server information
    if (sendServerInfo(naming_server_sock, "127.0.0.1", PORT, PORT + 2, root) < 0)
    {
        printf("Failed to send server information\n");
    }
    else
    {
        printf("Successfully registered with naming server\n");
    }
    int storage_server_sock;
    struct sockaddr_in storage_serv_addr;

    storage_server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (storage_server_sock < 0)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    storage_serv_addr.sin_family = AF_INET;
    storage_serv_addr.sin_addr.s_addr = INADDR_ANY;
    storage_serv_addr.sin_port = htons(PORT + 2); // Use different port for client connections

    // Enable address reuse
    int opt = 1;
    if (setsockopt(storage_server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("setsockopt failed");
        exit(EXIT_FAILURE);
    }

    if (bind(storage_server_sock, (struct sockaddr *)&storage_serv_addr, sizeof(storage_serv_addr)) < 0)
    {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(storage_server_sock, 10) < 0)
    {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Storage server is listening for client connections on port %d...\n", PORT + 1);

    // Main loop to accept client connections
    while (1)
    {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        int client_socket = accept(storage_server_sock, (struct sockaddr *)&client_addr, &addr_len);
        if (client_socket < 0)
        {
            perror("Accept failed");
            continue;
        }

        // Create client data structure
        struct ClientData *client_data = malloc(sizeof(struct ClientData));
        client_data->socket = client_socket;
        client_data->root = root;

        // Create thread for new client
        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handleClient, (void *)client_data) != 0)
        {
            perror("Failed to create thread");
            close(client_socket);
            free(client_data);
            continue;
        }

        // Detach thread to allow it to clean up automatically when it's done
        pthread_detach(thread_id);

        printf("New client connected. Assigned to thread %lu\n", (unsigned long)thread_id);
    }

    // Cleanup (this part won't be reached in this implementation)
    freeNode(root);
    close(storage_server_sock);
    // printUsage();

    // Cleanup
    freeNode(root);
    return 0;
}