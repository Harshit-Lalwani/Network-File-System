#include "header.h"
#define PORT 8080

int sendNodeChain(int sock, Node *node)
{
    Node *current = node;

    while (current != NULL)
    {
        // Send marker for valid node
        int valid_marker = 1;
        if (send(sock, &valid_marker, sizeof(int), 0) < 0)
            return -1;

        // Send node data
        int name_len = strlen(current->name) + 1;
        if (send(sock, &name_len, sizeof(int), 0) < 0)
            return -1;
        if (send(sock, current->name, name_len, 0) < 0)
            return -1;

        if (send(sock, &current->type, sizeof(NodeType), 0) < 0)
            return -1;
        if (send(sock, &current->permissions, sizeof(Permissions), 0) < 0)
            return -1;

        int loc_len = strlen(current->dataLocation) + 1;
        if (send(sock, &loc_len, sizeof(int), 0) < 0)
            return -1;
        if (send(sock, current->dataLocation, loc_len, 0) < 0)
            return -1;

        // If this node has children (is a directory)
        if (current->type == DIRECTORY_NODE && current->children != NULL)
        {
            // Send marker indicating has children
            int has_children = 1;
            if (send(sock, &has_children, sizeof(int), 0) < 0)
                return -1;

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
        }

        current = current->next;
    }

    // Send end of chain marker
    int end_marker = -1;
    if (send(sock, &end_marker, sizeof(int), 0) < 0)
        return -1;

    return 0;
}

// Function to send server information including the hash table
int sendServerInfo(int sock, const char *ip, int nm_port, int client_port, Node *root)
{
    // Send initialization message
    char init_msg = 'I';
    if (send(sock, &init_msg, sizeof(char), 0) < 0)
    {
        perror("Failed to send init message");
        return -1;
    }

    // Send server details
    if (send(sock, ip, 16, 0) < 0)
    {
        perror("Failed to send IP address");
        return -1;
    }
    if (send(sock, &nm_port, sizeof(int), 0) < 0)
    {
        perror("Failed to send NM port");
        return -1;
    }
    if (send(sock, &client_port, sizeof(int), 0) < 0)
    {
        perror("Failed to send client port");
        return -1;
    }

    // Send the root node and its entire structure
    return sendNodeChain(sock, root);
}



// Main function
int main()
{
    // Create root directory
    Node *root = createNode("home", DIRECTORY_NODE, READ | WRITE | EXECUTE, "/home");

    // Start traversal from the root directory
    traverseAndAdd(root, "/home");

    int client_sock1;
    struct sockaddr_in serv_addr;

    // Creating a socket
    client_sock1 = socket(AF_INET, SOCK_STREAM, 0);
    if (client_sock1 < 0)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Defining server address
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0)
    {
        perror("Invalid address or address not supported");
        exit(EXIT_FAILURE);
    }

    // Connecting to the server
    while (1)
    {
        if (connect(client_sock1, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
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
    if (sendServerInfo(client_sock1, "127.0.0.1", PORT, PORT + 1, root) < 0)
    {
        printf("Failed to send server information\n");
    }
    else
    {
        printf("Successfully registered with naming server\n");
    }
    printUsage();

    // Cleanup
    freeNode(root);
    return 0;
}