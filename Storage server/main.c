#include "header.h"
#define PORT 8080

AsyncWriteTask *asyncWriteQueue = NULL;                   // The head of the queue
pthread_mutex_t queueMutex = PTHREAD_MUTEX_INITIALIZER;   // Mutex for queue protection
pthread_cond_t queueCondition = PTHREAD_COND_INITIALIZER; // Condition variable for signaling

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
    memset(buffer, 0 , sizeof(buffer));
    recv(sock, buffer, sizeof(buffer), 0);
    // Send the root node and its entire structure
    return sendNodeChain(sock, root);
}

void *handleClient(void *arg)
{
    struct ClientData *data = (struct ClientData *)arg;
    int client_socket = data->socket;
    Node *root = data->root;
    char buffer[100001];

    while (1)
    {
        memset(buffer, 0, sizeof(buffer));
        ssize_t bytes_received = recv(client_socket, buffer, sizeof(buffer), 0);
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

// void *thread_process_command(void *arg)
// {
//     ThreadArgs *args = (ThreadArgs *)arg;

//     // Process the command
//     processCommand_namingServer(args->root, args->command, args->socket);

//     // Free the thread arguments
//     free(args);

//     return NULL;
// }

void *namingServerHandler(void *arg)
{
    struct ClientData *info = (struct ClientData *)arg;
    int naming_server_sock = info->socket;
    Node *root = info->root;

    while (1)
    {
        // Receive command from naming server
        char command[100001];
        memset(command, 0 , sizeof(command));
        ssize_t bytes_received = recv(naming_server_sock, command, sizeof(command), 0);

        if (bytes_received <= 0)
        {
            // Connection lost, attempt to reconnect
            printf("Lost connection to naming server. Attempting to reconnect...\n");
            close(naming_server_sock);

            // Recreate socket and attempt reconnection
            struct sockaddr_in naming_serv_addr;
            naming_server_sock = socket(AF_INET, SOCK_STREAM, 0);
            naming_serv_addr.sin_family = AF_INET;
            naming_serv_addr.sin_port = htons(8080);
            inet_pton(AF_INET, "127.0.0.1", &naming_serv_addr.sin_addr);

            while (connect(naming_server_sock, (struct sockaddr *)&naming_serv_addr,
                           sizeof(naming_serv_addr)) < 0)
            {
                sleep(5); // Wait before retry
            }

            // Reregister with naming server
            if (sendServerInfo(naming_server_sock, "127.0.0.1", PORT, PORT+2, root) < 0)
            {
                printf("Failed to re-register with naming server\n");
                continue;
            }
            printf("Successfully reconnected to naming server\n");
            info->socket = naming_server_sock;
            continue;
        }
        command[bytes_received] = '\0';
        printf("naming aaya\n");
        processCommand_namingServer(root, command, naming_server_sock);
    }
    return NULL;
}

void *periodicFlush(void *arg)
{
    printf("you");
    while (1)
    {
        printf("ova");
        pthread_mutex_lock(&queueMutex); // Lock the mutex before checking the queue

        // Wait if the queue is empty
        while (!asyncWriteQueue)
        {
            printf("are uou ");
            pthread_cond_wait(&queueCondition, &queueMutex); // Wait for a signal if the queue is empty
        }

        flushAsyncWrites();                // Call to periodically flush the queue
        pthread_mutex_unlock(&queueMutex); // Unlock the mutex after flushing

        sleep(1); // Sleep for 1 second before the next flush cycle
    }
    return NULL;
}
pthread_t flushThread;

// Add this to your cleanup code
void cleanupAsyncWriter()
{
    pthread_cancel(flushThread);
    pthread_mutex_destroy(&queueMutex);
    pthread_cond_destroy(&queueCondition);

    // Clean up any pending writes
    pthread_mutex_lock(&queueMutex);
    while (asyncWriteQueue)
    {
        AsyncWriteTask *task = asyncWriteQueue;
        asyncWriteQueue = asyncWriteQueue->next;
        free(task->data);
        free(task);
    }
    pthread_mutex_unlock(&queueMutex);
}

// Main function
int main()
{
    Node *root = createNode("/home", DIRECTORY_NODE, READ | WRITE | EXECUTE, "/home");
    traverseAndAdd(root, "/home");

    // Change lock types for specific test files
    Node *read_test = searchPath(root, "/testFile/read_test.txt");
    if (read_test)
    {
        read_test->lock_type = 1; // Set read lock
    }

    Node *write_test = searchPath(root, "/testFile/write_test.txt");
    if (write_test)
    {
        write_test->lock_type = 2; // Set write lock
    }

    // printFileSystemTree(root, 10);
    int naming_server_sock;
    struct sockaddr_in naming_naming_serv_addr;
    struct sockaddr_in naming_serv_addr;
    naming_server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (naming_server_sock < 0)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    naming_serv_addr.sin_family = AF_INET;
    naming_serv_addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, "127.0.0.1", &naming_serv_addr.sin_addr) <= 0)
    {
        perror("Invalid address or address not supported");
        exit(EXIT_FAILURE);
    }
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
    if (sendServerInfo(naming_server_sock, "127.0.0.1", PORT, PORT+2, root) < 0)
    {
        printf("Failed to send server information\n");
    }
    else
    {
        printf("Successfully registered with naming server\n");
    }
    pthread_t naming_server_thread;
    struct ClientData *server_info = malloc(sizeof(struct ClientData));
    server_info->socket = naming_server_sock;
    server_info->root = root;
    if (pthread_create(&naming_server_thread, NULL, namingServerHandler, server_info) != 0)
    {
        perror("Failed to create naming server handler thread");
        exit(EXIT_FAILURE);
    }
    pthread_detach(naming_server_thread);
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
    storage_serv_addr.sin_port = htons(PORT+2);
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
    if (pthread_create(&flushThread, NULL, periodicFlush, NULL) != 0)
    {
        perror("Failed to create flush thread");
        return 1;
    }

    // Wait for the flush thread to finish (in this case, it runs indefinitely)
    pthread_detach(flushThread);
    printf("Storage server is listening for client connections on port %d...\n", PORT+2);
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
        struct ClientData *client_data = malloc(sizeof(struct ClientData));
        client_data->socket = client_socket;
        client_data->root = root;
        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handleClient, (void *)client_data) != 0)
        {
            perror("Failed to create thread");
            close(client_socket);
            free(client_data);
            continue;
        }
        pthread_detach(thread_id);
        printf("New client connected. Assigned to thread %lu\n", (unsigned long)thread_id);
    }
    freeNode(root);
    close(storage_server_sock);
    freeNode(root);
    return 0;
}