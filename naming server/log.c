#include "header.h"

// Function to get the current timestamp
void get_timestamp(char *timestamp, size_t size) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(timestamp, size, "%Y-%m-%d %H:%M:%S", tm_info);
}

// Function to log the message
void log_message(const char *ip, int port, const char *role, const char *message) {
    // Lock the mutex before writing to the log
    pthread_mutex_lock(&log_mutex);

    // Open the log file for appending
    FILE *log_file = fopen(log_file_path, "a");
    if (log_file == NULL) {
        perror("Failed to open log file");
        pthread_mutex_unlock(&log_mutex);  // Unlock mutex before returning
        return;
    }

    // Get the current timestamp
    char timestamp[20];
    get_timestamp(timestamp, sizeof(timestamp));

    // If IP and Port are 0, log only timestamp and message
    if (ip == NULL || port == 0) {
        fprintf(log_file, "[%s] %s\n", timestamp, message);
    } else {
        // Format log entry: timestamp, IP, port, role, message
        fprintf(log_file, "[%s] %s:%d %s %s\n", timestamp, ip, port, role, message);
    }

    // Close the log file
    fclose(log_file);

    // Unlock the mutex
    pthread_mutex_unlock(&log_mutex);
}

// Function to get the IP and port from a socket address (sockaddr_in)
void get_ip_and_port(struct sockaddr_in *sa, char *ip_buffer, int *port) {
    // Get IP address from sockaddr_in
    inet_ntop(AF_INET, &(sa->sin_addr), ip_buffer, INET_ADDRSTRLEN);
    *port = ntohs(sa->sin_port);  // Convert port from network byte order to host byte order
}
