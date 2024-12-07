#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>

#define SERVER_PORT 21
#define BUFFER_SIZE 1024

static int transfer_type = 0; // 0 = Binary (default), 1 = ASCII
const char *restricted_dir = "/allowed_directory"; // Restrict file access to this directory

// Function to send FTP response to the client
void send_response(int client_sock, const char *response) {
    write(client_sock, response, strlen(response));
}

// Validate file path to restrict access outside the allowed directory
int validate_path(const char *path) {
    // Prevent access outside restricted directory
    if (strstr(path, "..") != NULL || strstr(path, restricted_dir) == NULL) {
        return 0; // Invalid path
    }
    return 1; // Valid path
}

// USER command (Authentication step)
void handle_user_command(int client_sock) {
    // Requirement: Authentication (USER command)
    send_response(client_sock, "331 Username OK, need password.\r\n");
}

// PASS command (Authentication step)
void handle_pass_command(int client_sock) {
    // Requirement: Authentication (PASS command)
    send_response(client_sock, "230 User logged in, proceed.\r\n");
}

// LIST command (Retrieve directory listing)
void handle_list_command(int client_sock) {
    // Requirement: Support for LIST Command (Retrieve directory listing)
    DIR *dir;
    struct dirent *ent;
    char response[BUFFER_SIZE] = "150 Here comes the directory listing.\r\n";

    send_response(client_sock, response);

    if ((dir = opendir(restricted_dir)) != NULL) {
        while ((ent = readdir(dir)) != NULL) {
            snprintf(response, sizeof(response), "%s\r\n", ent->d_name);
            send_response(client_sock, response);
        }
        closedir(dir);
    } else {
        send_response(client_sock, "550 Failed to open directory.\r\n");
    }

    send_response(client_sock, "226 Directory send OK.\r\n");
}

// TYPE command (Handle ASCII/Binary modes)
void handle_type_command(int client_sock, const char *buffer) {
    // Requirement: Data Transfer Types (TYPE A for ASCII, TYPE I for Binary)
    if (strncmp(buffer + 5, "A", 1) == 0) {
        transfer_type = 1; // ASCII mode
        send_response(client_sock, "200 Type set to A (ASCII mode).\r\n");
    } else if (strncmp(buffer + 5, "I", 1) == 0) {
        transfer_type = 0; // Binary mode
        send_response(client_sock, "200 Type set to I (Binary mode).\r\n");
    } else {
        send_response(client_sock, "504 Command not implemented for that parameter.\r\n");
    }
}

// GET command (Download file from server - RETR)
void handle_get_command(int client_sock, const char *filename) {
    // Requirement: Support for GET Command (RETR - Download file)
    char response[BUFFER_SIZE];
    char file_path[BUFFER_SIZE];

    snprintf(file_path, sizeof(file_path), "%s/%s", restricted_dir, filename);
    if (!validate_path(file_path)) {
        snprintf(response, sizeof(response), "550 Access denied.\r\n");
        send_response(client_sock, response);
        return;
    }

    FILE *file = fopen(file_path, transfer_type == 0 ? "rb" : "r");
    if (file == NULL) {
        snprintf(response, sizeof(response), "550 File not found or access denied.\r\n");
        send_response(client_sock, response);
        return;
    }

    send_response(client_sock, "150 Opening data connection.\r\n");

    char data[BUFFER_SIZE];
    size_t bytes_read;
    while ((bytes_read = fread(data, 1, sizeof(data), file)) > 0) {
        if (transfer_type == 1) { // Convert to ASCII if needed
            for (size_t i = 0; i < bytes_read; ++i) {
                if (data[i] == '\n') write(client_sock, "\r", 1);
                write(client_sock, &data[i], 1);
            }
        } else {
            write(client_sock, data, bytes_read);
        }
    }

    fclose(file);
    send_response(client_sock, "226 Transfer complete.\r\n");
}

// PUT command (Upload file to server - STOR)
void handle_put_command(int client_sock, const char *filename) {
    // Requirement: Support for PUT Command (STOR - Upload file)
    char response[BUFFER_SIZE];
    char file_path[BUFFER_SIZE];

    snprintf(file_path, sizeof(file_path), "%s/%s", restricted_dir, filename);
    if (!validate_path(file_path)) {
        snprintf(response, sizeof(response), "550 Access denied.\r\n");
        send_response(client_sock, response);
        return;
    }

    FILE *file = fopen(file_path, transfer_type == 0 ? "wb" : "w");
    if (file == NULL) {
        snprintf(response, sizeof(response), "550 Cannot create file.\r\n");
        send_response(client_sock, response);
        return;
    }

    send_response(client_sock, "150 Ready to receive data.\r\n");

    char data[BUFFER_SIZE];
    ssize_t bytes_read;
    while ((bytes_read = read(client_sock, data, sizeof(data))) > 0) {
        if (transfer_type == 1) { // Convert from ASCII if needed
            for (ssize_t i = 0; i < bytes_read; ++i) {
                if (data[i] == '\r') continue; // Ignore '\r' in ASCII mode
                fwrite(&data[i], 1, 1, file);
            }
        } else {
            fwrite(data, 1, bytes_read, file);
        }
    }

    fclose(file);
    send_response(client_sock, "226 Transfer complete.\r\n");
}

// RNFR and RNTO commands (Rename file)
void handle_rename_command(int client_sock, const char *buffer) {
    // Requirement: Rename functionality (RNFR and RNTO)
    static char old_name[BUFFER_SIZE];
    char response[BUFFER_SIZE];

    if (strncmp(buffer, "RNFR", 4) == 0) {
        snprintf(old_name, sizeof(old_name), "%s", buffer + 5);
        old_name[strcspn(old_name, "\r\n")] = '\0';
        send_response(client_sock, "350 Ready for destination name.\r\n");
    } else if (strncmp(buffer, "RNTO", 4) == 0) {
        char new_name[BUFFER_SIZE];
        snprintf(new_name, sizeof(new_name), "%s", buffer + 5);
        new_name[strcspn(new_name, "\r\n")] = '\0';

        if (rename(old_name, new_name) == 0) {
            send_response(client_sock, "250 File renamed successfully.\r\n");
        } else {
            send_response(client_sock, "550 Rename failed.\r\n");
        }
    }
}

// DELE command (Delete file)
void handle_delete_command(int client_sock, const char *filename) {
    // Requirement: Support for DELETE Command (DELE)
    char response[BUFFER_SIZE];
    char file_path[BUFFER_SIZE];

    snprintf(file_path, sizeof(file_path), "%s/%s", restricted_dir, filename);
    if (!validate_path(file_path)) {
        snprintf(response, sizeof(response), "550 Access denied.\r\n");
        send_response(client_sock, response);
        return;
    }

    if (unlink(file_path) == 0) {
        snprintf(response, sizeof(response), "250 File deleted successfully.\r\n");
    } else {
        snprintf(response, sizeof(response), "550 File not found or cannot delete.\r\n");
    }

    send_response(client_sock, response);
}

// Handle client commands
void handle_client(int client_sock) {
    char buffer[BUFFER_SIZE];
    ssize_t n;

    send_response(client_sock, "220 Welcome to Simple FTP Server\r\n");

    while ((n = read(client_sock, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[n] = '\0';
        printf("Received command: %s", buffer);

        if (strncmp(buffer, "USER", 4) == 0) {
            handle_user_command(client_sock);
        } else if (strncmp(buffer, "PASS", 4) == 0) {
            handle_pass_command(client_sock);
        } else if (strncmp(buffer, "LIST", 4) == 0) {
            handle_list_command(client_sock);
        } else if (strncmp(buffer, "GET ", 4) == 0) {
            handle_get_command(client_sock, buffer + 4);
        } else if (strncmp(buffer, "PUT ", 4) == 0) {
            handle_put_command(client_sock, buffer + 4);
        } else if (strncmp(buffer, "TYPE", 4) == 0) {
            handle_type_command(client_sock, buffer);
        } else if (strncmp(buffer, "RNFR", 4) == 0 || strncmp(buffer, "RNTO", 4) == 0) {
            handle_rename_command(client_sock, buffer);
        } else if (strncmp(buffer, "DELE ", 5) == 0) {
            handle_delete_command(client_sock, buffer + 5);
        } else if (strncmp(buffer, "QUIT", 4) == 0) {
            send_response(client_sock, "221 Goodbye.\r\n");
            break;
        } else {
            send_response(client_sock, "502 Command not implemented.\r\n");
        }
    }

    close(client_sock);
}

// Main function
int main() {
    int server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    if ((server_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation error");
        exit(1);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);

    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_sock);
        exit(1);
    }

    if (listen(server_sock, 5) < 0) {
        perror("Listen failed");
        close(server_sock);
        exit(1);
    }

    printf("FTP server started on port %d\n", SERVER_PORT);

    while ((client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &client_addr_len)) > 0) {
        printf("Client connected\n");
        handle_client(client_sock);
        printf("Client disconnected\n");
    }

    close(server_sock);
    return 0;
}
