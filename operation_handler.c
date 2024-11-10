#include"heaher.h"

CommandType parseCommand(const char *cmd)
{
    if (strcasecmp(cmd, "READ") == 0)
        return CMD_READ;
    if (strcasecmp(cmd, "WRITE") == 0)
        return CMD_WRITE;
    if (strcasecmp(cmd, "META") == 0)
        return CMD_META;
    if (strcasecmp(cmd, "STREAM") == 0)
        return CMD_STREAM;
    if (strcasecmp(cmd, "CREATE") == 0)
        return CMD_CREATE;
    if (strcasecmp(cmd, "DELETE") == 0)
        return CMD_DELETE;
    if (strcasecmp(cmd, "COPY") == 0)
        return CMD_COPY;
    return CMD_UNKNOWN;
}

// Update the usage information
void printUsage()
{
    printf("\nAvailable commands:\n");
    printf("READ <path>                    - Read contents of a file\n");
    printf("WRITE <path>                   - Write content to a file\n");
    printf("META <path>                    - Get file metadata\n");
    printf("STREAM <path>                  - Stream an audio file\n");
    printf("CREATE FILE <path>             - Create an empty file\n");
    printf("CREATE DIR <path>              - Create an empty directory\n");
    printf("DELETE <path>                  - Delete a file or directory\n");
    printf("COPY <source> <destination>    - Copy file or directory\n");
    printf("EXIT                           - Exit the program\n");
}

void processCommand(Node *root)
{
    char input[MAX_COMMAND_LENGTH];
    char command[MAX_COMMAND_LENGTH];
    char path[MAX_PATH_LENGTH];
    char buffer[MAX_CONTENT_LENGTH];
    char secondPath[MAX_PATH_LENGTH];
    char typeStr[5];
    struct stat metadata;

    while (1)
    {
        printf("\nEnter command and path (or 'EXIT' to quit): ");

        // Read entire line of input
        if (fgets(input, sizeof(input), stdin) == NULL)
        {
            printf("Error reading input. Exiting...\n");
            break;
        }

        // Remove trailing newline
        size_t len = strlen(input);
        if (len > 0 && input[len - 1] == '\n')
        {
            input[len - 1] = '\0';
            len--;
        }

        // Check for empty input
        if (len == 0)
        {
            printf("Empty input. Please enter a command.\n");
            printUsage();
            continue;
        }

        // Handle EXIT command
        if (strcasecmp(input, "EXIT") == 0)
        {
            printf("Exiting...\n");
            break;
        }

        // Parse command
        char *tok = strtok(input, " ");
        if (!tok)
        {
            printf("Invalid input format. Expected: COMMAND PATH\n");
            printUsage();
            continue;
        }
        strncpy(command, tok, sizeof(command) - 1);
        command[sizeof(command) - 1] = '\0';

        CommandType cmd = parseCommand(command);

        // Handle different command types
        switch (cmd)
        {
        case CMD_READ:
        case CMD_WRITE:
        case CMD_META:
        case CMD_STREAM:
            // Get path for basic commands
            tok = strtok(NULL, " ");
            if (!tok)
            {
                printf("Error: Path is required\n");
                continue;
            }
            strncpy(path, tok, sizeof(path) - 1);
            path[sizeof(path) - 1] = '\0';

            // Validate path
            Node *targetNode = searchPath(root, path);
            if (!targetNode)
            {
                printf("Path not found: %s\n", path);
                continue;
            }

            // Process basic commands
            if (cmd == CMD_READ)
            {
                ssize_t bytes = readFile(targetNode, buffer, sizeof(buffer) - 1);
                if (bytes > 0)
                {
                    buffer[bytes] = '\0';
                    printf("\n--- File Contents ---\n%s\n------------------\n", buffer);
                }
            }
            else if (cmd == CMD_WRITE)
            {
                printf("Enter content to write (max %zu chars, press Enter when done):\n",
                       sizeof(buffer) - 1);

                if (fgets(buffer, sizeof(buffer), stdin) != NULL)
                {
                    len = strlen(buffer);
                    if (len > 0 && buffer[len - 1] == '\n')
                    {
                        buffer[len - 1] = '\0';
                        len--;
                    }

                    ssize_t bytes = writeFile(targetNode, buffer, len);
                    if (bytes > 0)
                    {
                        printf("Successfully wrote %zd bytes\n", bytes);
                    }
                }
            }
            else if (cmd == CMD_META)
            {
                if (getFileMetadata(targetNode, &metadata) == 0)
                {
                    printf("\n--- File Metadata ---\n");
                    printf("Name: %s\n", targetNode->name);
                    printf("Type: %s\n", targetNode->type == FILE_NODE ? "File" : "Directory");
                    printf("Size: %ld bytes\n", metadata.st_size);
                    printf("Permissions: %o\n", metadata.st_mode & 0777);
                    printf("Last access: %s", ctime(&metadata.st_atime));
                    printf("Last modification: %s", ctime(&metadata.st_mtime));
                    printf("------------------\n");
                }
            }
            else if (cmd == CMD_STREAM)
            {
                printf("\nStreaming audio file: %s\n", targetNode->name);
                off_t offset = 0;
                ssize_t bytes;
                int chunks = 0;

                while ((bytes = streamAudioFile(targetNode, buffer, CHUNK_SIZE, offset)) > 0)
                {
                    printf("Chunk %d: Streamed %zd bytes from offset %ld\n",
                           ++chunks, bytes, offset);
                    offset += bytes;

                    if (chunks >= 5)
                    {
                        printf("Demo stream limited to 5 chunks...\n");
                        break;
                    }
                }
            }
            break;

        case CMD_CREATE:
            // Get type (FILE or DIR)
            tok = strtok(NULL, " ");
            if (!tok)
            {
                printf("Error: Specify FILE or DIR\n");
                break;
            }
            strncpy(typeStr, tok, sizeof(typeStr) - 1);
            typeStr[sizeof(typeStr) - 1] = '\0';

            // Get path
            tok = strtok(NULL, "");
            if (!tok)
            {
                printf("Error: Specify path\n");
                break;
            }
            strncpy(path, tok, sizeof(path) - 1);
            path[sizeof(path) - 1] = '\0';

            // Extract parent path and name
            char *lastSlash = strrchr(path, '/');
            if (!lastSlash)
            {
                printf("Error: Invalid path format\n");
                break;
            }

            *lastSlash = '\0';
            char *name = lastSlash + 1;
            Node *parentDir = searchPath(root, path);
            *lastSlash = '/';

            if (!parentDir)
            {
                printf("Error: Parent directory not found\n");
                break;
            }

            NodeType type = (strcasecmp(typeStr, "DIR") == 0) ? DIRECTORY_NODE : FILE_NODE;
            if (createEmptyNode(parentDir, name, type))
            {
                printf("Successfully created %s: %s\n",
                       type == DIRECTORY_NODE ? "directory" : "file", path);
            }
            break;

        case CMD_COPY:
            // Get source path
            tok = strtok(NULL, " ");
            if (!tok)
            {
                printf("Error: Specify source path\n");
                break;
            }
            strncpy(path, tok, sizeof(path) - 1);
            path[sizeof(path) - 1] = '\0';

            // Get destination path
            tok = strtok(NULL, "");
            if (!tok)
            {
                printf("Error: Specify destination path\n");
                break;
            }
            strncpy(secondPath, tok, sizeof(secondPath) - 1);
            secondPath[sizeof(secondPath) - 1] = '\0';

            Node *sourceNode = searchPath(root, path);
            if (!sourceNode)
            {
                printf("Error: Source path not found\n");
                break;
            }

            lastSlash = strrchr(secondPath, '/');
            if (!lastSlash)
            {
                printf("Error: Invalid destination path format\n");
                break;
            }

            *lastSlash = '\0';
            name = lastSlash + 1;
            Node *destDir = searchPath(root, secondPath);

            if (!destDir)
            {
                printf("Error: Destination directory not found\n");
                break;
            }

            if (copyNode(sourceNode, destDir, name) == 0)
            {
                printf("Successfully copied to: %s/%s\n", secondPath, name);
            }
            break;

        case CMD_DELETE:
            tok = strtok(NULL, "");
            if (!tok)
            {
                printf("Error: Specify path\n");
                break;
            }
            strncpy(path, tok, sizeof(path) - 1);
            path[sizeof(path) - 1] = '\0';

            Node *nodeToDelete = searchPath(root, path);
            if (!nodeToDelete)
            {
                printf("Error: Path not found\n");
                break;
            }

            if (deleteNode(nodeToDelete) == 0)
            {
                printf("Successfully deleted: %s\n", path);
            }
            break;

        case CMD_UNKNOWN:
            printf("Unknown command: %s\n", command);
            printUsage();
            break;
        }
    }
}
