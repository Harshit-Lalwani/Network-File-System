#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <pthread.h>

#define MAX_BUFFER_SIZE 100001
#define SERVER_PORT 8080
#define SERVER_IP "127.0.0.1" // Change this to your server's IP
#define ACK_RECEIVE_PORT 9091 // Dedicated port for receiving ACKs
int ack_socket;               // Declare globally to be accessed by both functions
struct sockaddr_in ack_addr;

void displayHelp()
{
    printf("\nAvailable commands:\n");
    printf("READ <path> - Read file content\n");
    printf("WRITE <path> - Write content to file\n");
    printf("DELETE <path> - Delete a file or folder\n");
    printf("CREATE FILE/DIR <no> <path> - Create a new file or folder\n");
    printf("LIST <path> - List all files and folders in the specified directory\n");
    printf("META <path> - Get file metadata\n");
    printf("STREAM <path> - Stream file content\n");
    printf("EXIT - Close connection and exit\n");

    printf("HELP - Display this help message\n\n");
}

//  WRITE DISPLAY EROR CODES ETC TOOO

// Modify the structure to return both socket and port
struct ServerInfo
{
    char ip[20];
    int port;
};

int connectToServer(const char *ip, int port)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        perror("Socket creation failed");
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0)
    {
        perror("Invalid address");
        close(sock);
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Connection failed");
        close(sock);
        return -1;
    }

    return sock;
}

void initializeAckSocket()
{
    // Create socket to receive acknowledgment messages
    ack_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (ack_socket < 0)
    {
        perror("Socket creation failed for acknowledgment");
        exit(-1);
    }

    // Set up the acknowledgment socket address
    ack_addr.sin_family = AF_INET;
    ack_addr.sin_port = htons(ACK_RECEIVE_PORT);
    ack_addr.sin_addr.s_addr = INADDR_ANY;

    // Bind the socket to the address and port
    if (bind(ack_socket, (struct sockaddr *)&ack_addr, sizeof(ack_addr)) < 0)
    {
        perror("Binding failed for acknowledgment socket");
        close(ack_socket);
        exit(-1);
    }

    // Start listening for incoming ACKs
    if (listen(ack_socket, 1) < 0)
    {
        perror("Listen failed for acknowledgment socket");
        close(ack_socket);
        exit(-1);
    }

    printf("Client is listening for ACKs on port %d...\n", ACK_RECEIVE_PORT);
}
pthread_t ack_thread;

void *ackReceiver(void *arg)
{
    int new_sock;
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    char buffer[1024];

    // Accept the incoming connection from the naming server
    new_sock = accept(ack_socket, (struct sockaddr *)&client_addr, &addr_len);
    if (new_sock < 0)
    {
        perror("Failed to accept ACK connection");
        return NULL;
    }
    memset(buffer, 0 , sizeof(buffer));
    // Receive the acknowledgment message
    ssize_t recv_size = recv(new_sock, buffer, sizeof(buffer) - 1, 0);
    if (recv_size > 0)
    {
        buffer[recv_size] = '\0'; // Null-terminate the received message
        printf("Received ACK: %s\n", buffer);
    }
    else
    {
        perror("Failed to receive acknowledgment");
    }

    close(new_sock);
    return NULL;
}

void handleRead(int sock, const char *command)
{
    char buffer[MAX_BUFFER_SIZE];
    ssize_t bytes_received;

    // Send command to server
    send(sock, command, strlen(command), 0);

    // First receive file size
    memset(buffer, 0, sizeof(buffer));
    bytes_received = recv(sock, buffer, sizeof(buffer), 0);
    buffer[bytes_received] = '\0';
    send(sock, command, strlen(command), 0);

    if (strncmp(buffer, "FILE_SIZE:", 10) == 0)
    {
        long fileSize;
        sscanf(buffer, "FILE_SIZE:%ld", &fileSize);
        printf("Receiving file of size: %ld bytes\n", fileSize);
        long size = 0;
        // Receive file content
        memset(buffer, 0, sizeof(buffer));
        while ((bytes_received = recv(sock, buffer, sizeof(buffer), 0)) > 0)
        {
            send(sock, command, strlen(command), 0);
            // printf("%d\n",bytes_received);
            // size+=bytes_received;
            buffer[bytes_received] = '\0';
            if (strncmp(buffer, "END_OF_FILE\n", 12) == 0)
                break;
            printf("%s", buffer);
            memset(buffer, 0, sizeof(buffer));
        }
    }
    else
    {
        printf("Error: %s", buffer);
    }
}

// void handleWrite(int sock, const char *command)
// {
//     char buffer[MAX_BUFFER_SIZE];
//     char filepath[256];
//     FILE *file;

//     // Extract filepath from command
//     sscanf(command, "WRITE %s", filepath);

//     // Open local file
//     file = fopen(filepath, "rb");
//     if (!file)
//     {
//         printf("Error: Cannot open local file %s\n", filepath);
//         return;
//     }

//     // Get file size
//     fseek(file, 0, SEEK_END);
//     long fileSize = ftell(file);
//     fseek(file, 0, SEEK_SET);

//     // Send command to server
//     send(sock, command, strlen(command), 0);

//     // Send file size
//     snprintf(buffer, sizeof(buffer), "FILE_SIZE:%ld", fileSize);
//     send(sock, buffer, strlen(buffer), 0);

//     // Wait for server acknowledgment
//     recv(sock, buffer, sizeof(buffer), 0);
//     if (strcmp(buffer, "READY_TO_RECEIVE\n") != 0)
//     {
//         printf("Error: Server not ready to receive\n");
//         fclose(file);
//         return;
//     }

//     // Send file content in chunks
//     size_t bytes_read;
//     while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0)
//     {
//         if (send(sock, buffer, bytes_read, 0) != bytes_read)
//         {
//             printf("Error sending file data\n");
//             break;
//         }
//     }

//     fclose(file);

//     // Receive confirmation
//     recv(sock, buffer, sizeof(buffer), 0);
//     printf("%s", buffer);
// }

void handleWrite(int sock, const char *command)
{
    char buffer[MAX_BUFFER_SIZE];
    char content[MAX_BUFFER_SIZE * 16]; // Larger buffer for user input
    char filepath[256];

    // Extract filepath from command
    sscanf(command, "WRITE %s", filepath);

    // Get content size from user
    printf("Enter the number of characters to write: ");

    long contentSize;
    if (scanf("%ld", &contentSize) != 1)
    {
        printf("Error: Invalid content size\n");
        return;
    }

    // Clear any newline character from stdin
    int c;
    while ((c = getchar()) != '\n' && c != EOF)
        ;

    // Get content from user
    printf("Enter the content (max %ld characters):\n", contentSize);

    // Read input until desired size is reached or buffer is full
    size_t total_size = 0;
    while (total_size < (size_t)contentSize)
    {
        if (!fgets(buffer, sizeof(buffer), stdin))
        {
            if (ferror(stdin))
            {
                printf("Error reading input\n");
                return;
            }
            break; // EOF reached
        }

        size_t input_len = strlen(buffer);

        // Check if buffer would overflow
        if (total_size + input_len >= sizeof(content))
        {
            printf("Input too large, truncating...\n");
            break;
        }

        // Append input to content buffer
        strcat(content, buffer);
        total_size += input_len;
        printf("%s\n", buffer);
        memset(buffer, 0, sizeof(buffer));
    }

    // Clear any EOF condition
    clearerr(stdin);

    // Send command to server
    send(sock, command, strlen(command), 0);
    memset(buffer,0, sizeof(buffer));
    recv(sock, buffer, sizeof(buffer), 0);
    // Send content size
    memset(buffer,0, sizeof(buffer));
    snprintf(buffer, sizeof(buffer), "FILE_SIZE:%ld", contentSize);
    send(sock, buffer, strlen(buffer), 0);
    // recv(sock, buffer, sizeof(buffer), 0);

    memset(buffer,0, sizeof(buffer));
    // Wait for server acknowledgment
    ssize_t recv_size = recv(sock, buffer, sizeof(buffer), 0);
    if (recv_size <= 0)
    {
        printf("Error receiving server acknowledgment\n");
        return;
    }
    buffer[recv_size] = '\0';

    if (strcmp(buffer, "READY_TO_RECEIVE\n") != 0)
    {
        printf("Error: Server not ready to receive\n");
        return;
    }

    // Send content in chunks
    size_t remaining = contentSize;
    size_t offset = 0;

    while (remaining > 0)
    {
        size_t chunk_size = (remaining < MAX_BUFFER_SIZE) ? remaining : MAX_BUFFER_SIZE;
        ssize_t sent = send(sock, content + offset, chunk_size, 0);

        if (sent <= 0)
        {
            printf("Error sending data\n");
            return;
        }

        remaining -= sent;
        offset += sent;
        recv(sock, buffer, sizeof(buffer), 0);      
    }
    memset(buffer,0, sizeof(buffer));
    send(sock, "bruh\0", 5, 0);

    // Receive confirmation
    recv_size = recv(sock, buffer, sizeof(buffer), 0);
    if (recv_size <= 0)
    {
        printf("Error receiving server confirmation\n");
        return;
    }
    buffer[recv_size] = '\0';
    memset(content, 0 , sizeof(content));
    printf("%s", buffer);
}
void handleMeta(int sock, const char *command)
{
    char buffer[MAX_BUFFER_SIZE];

    send(sock, command, strlen(command), 0);
    memset(buffer,0, sizeof(buffer));
    ssize_t bytes_received = recv(sock, buffer, sizeof(buffer), 0);
    buffer[bytes_received] = '\0';
    printf("%s", buffer);
}

void handleStream(int sock, const char *command)
{
    char buffer[100001];
    ssize_t bytes_received;
    int pipe_fd[2];
    pid_t ffplay_pid;
    int stop_stream = 0;

    if (pipe(pipe_fd) == -1)
    {
        perror("Pipe creation failed");
        return;
    }

    ffplay_pid = fork();
    if (ffplay_pid == -1)
    {
        perror("Fork failed");
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        return;
    }

    if (ffplay_pid == 0)
    {
        close(pipe_fd[1]);
        dup2(pipe_fd[0], STDIN_FILENO);
        close(pipe_fd[0]);
        execlp("ffplay", "ffplay", "-i", "pipe:0", "-nodisp", "-autoexit", "-loglevel", "quiet", NULL);
        perror("Failed to execute ffplay");
        exit(1);
    }

    close(pipe_fd[0]);

    send(sock, command, strlen(command), 0);

    memset(buffer,0, sizeof(buffer));
    bytes_received = recv(sock, buffer, sizeof(buffer), 0);
    buffer[bytes_received] = '\0';

    if (strcmp(buffer, "START_STREAM\n") == 0)
    {
        printf("Stream started...\n");

        while ((bytes_received = recv(sock, buffer, sizeof(buffer), 0)) > 0)
        {
            buffer[bytes_received] = '\0';

            send(sock, command, strlen(command), 0);

            if (strcmp(buffer, "END_STREAM\n") == 0)
            {
                break;
            }

            write(pipe_fd[1], buffer, bytes_received);
            printf("Streaming chunk: %zd bytes\n", bytes_received);
            memset(buffer, 0, sizeof(buffer));
            if (stop_stream)
            {
                break;
            }
        }

        printf("Stream complete.\n");
    }
    else
    {
        printf("Error: %s", buffer);
    }

    close(pipe_fd[1]);

    kill(ffplay_pid, SIGTERM);
    int status;
    waitpid(ffplay_pid, &status, 0);
}

struct ServerInfo connect_naming_server(int sock, char *command)
{
    struct ServerInfo server = {"", 0}; // Initialize with empty IP and port 0

    send(sock, command, strlen(command), 0);
    char buffer[100001];
    memset(buffer,0, sizeof(buffer));
    recv(sock, buffer, sizeof(buffer), 0);

    // Check if path not found
    if (strcmp(buffer, "Path not found in any storage server") == 0)
    {
        return server; // Return with null port (0)
    }

    sscanf(buffer, "StorageServer: %s : %d", server.ip, &server.port);
    printf("%d %s", server.port, server.ip);
    return server;
}
int main()
{
    int naming_sock = connectToServer(SERVER_IP, 8081);
    if (naming_sock < 0)
    {
        return 1;
    }

    printf("Connected to server at %s:%d\n", SERVER_IP, 8081);
    displayHelp();

    initializeAckSocket();
    // Create a thread to listen for ACKs
    if (pthread_create(&ack_thread, NULL, ackReceiver, NULL) != 0)
    {
        perror("Failed to create thread for ACK reception");
        return -1;
    }

    char command[MAX_BUFFER_SIZE];

    while (1)
    {
        printf("\nEnter command: ");
        if (!fgets(command, sizeof(command), stdin))
            break;

        // Remove newline
        command[strcspn(command, "\n")] = 0;

        if (strlen(command) == 0)
            continue;

        if (strcmp(command, "HELP") == 0)
        {
            displayHelp();
            continue;
        }

        if (strcmp(command, "EXIT") == 0)
        {
            send(naming_sock, command, strlen(command), 0);
            break;
        }

        if (strncmp(command, "READ ", 5) == 0)
        {
            struct ServerInfo storage_server = connect_naming_server(naming_sock, command);
            if (storage_server.port == 0)
            {
                printf("Path not found in any storage server\n");
                continue;
            }
            int storage_sock = connectToServer(storage_server.ip, storage_server.port);
            if (storage_sock < 0)
            {
                continue;
            }
            handleRead(storage_sock, command);
            close(storage_sock);
        }
        else if (strncmp(command, "WRITE ", 6) == 0)
        {
            struct ServerInfo storage_server = connect_naming_server(naming_sock, command);
            if (storage_server.port == 0)
            {
                printf("Path not found in any storage server\n");
                continue;
            }
            int storage_sock = connectToServer(storage_server.ip, storage_server.port);
            if (storage_sock < 0)
            {
                continue;
            }
            handleWrite(storage_sock, command);
            close(storage_sock);
        }
        else if (strncmp(command, "META ", 5) == 0)
        {
            struct ServerInfo storage_server = connect_naming_server(naming_sock, command);
            if (storage_server.port == 0)
            {
                printf("Path not found in any storage server\n");
                continue;
            }
            int storage_sock = connectToServer(storage_server.ip, storage_server.port);
            if (storage_sock < 0)
            {
                continue;
            }
            handleMeta(storage_sock, command);
            close(storage_sock);
        }
        else if (strncmp(command, "STREAM ", 7) == 0)
        {
            struct ServerInfo storage_server = connect_naming_server(naming_sock, command);
            if (storage_server.port == 0)
            {
                printf("Path not found in any storage server\n");
                continue;
            }
            int storage_sock = connectToServer(storage_server.ip, storage_server.port);
            if (storage_sock < 0)
            {
                continue;
            }
            handleStream(storage_sock, command);
            close(storage_sock);
        }
        else if (strncmp(command, "CREATE ", 7) == 0)
        {
            char respond[100001];
            send(naming_sock, command, strlen(command), 0);
            memset(respond,0, sizeof(respond));
            recv(naming_sock, respond, sizeof(respond), 0);
            printf("%s\n", respond);
        }
        else if (strncmp(command, "DELETE ", 7) == 0)
        {
            char respond[100001];
            send(naming_sock, command, strlen(command), 0);
            memset(respond,0, sizeof(respond));
            recv(naming_sock, respond, sizeof(respond), 0);
            printf("%s\n", respond);
        }
        else if (strncmp(command, "COPY ", 5) == 0)
        {
            char respond[100001];
            send(naming_sock, command, strlen(command), 0);
            memset(respond,0, sizeof(respond));
            recv(naming_sock, respond, sizeof(respond), 0);
            printf("%s\n", respond);
        }
        else if (strncmp(command, "LIST", 4) == 0)
        {
            char response[100001] = {0}; // Buffer for response

            // Send the LIST command to the naming server
            send(naming_sock, command, strlen(command), 0);

            memset(response,0, sizeof(response));
            // Receive the response from the server
            int bytes_received = recv(naming_sock, response, sizeof(response) - 1, 0);

            if (bytes_received > 0)
            {
                response[bytes_received] = '\0'; // Null-terminate the response
                printf("List of files and directories:\n%s", response);
            }
            else
            {
                printf("Error: Unable to receive response from naming server.\n");
            }
        }
        else
        {
            printf("Invalid command. Type HELP for available commands.\n");
        }
    }

    close(naming_sock);
    printf("Connection closed.\n");
    return 0;
}

