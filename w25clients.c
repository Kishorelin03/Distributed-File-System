// w25clients.c - Client Program for COMP8567 DFS
// This client program connects to the main server (S1) of the distributed file system,
// sends user commands (upload, download, remove, etc.), and processes responses.
// It supports uploading a file, downloading a single file or an archive, and viewing file lists.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <libgen.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 7010
#define BUFSIZE 1024
#define MAX_RETRIES 3  // Maximum number of connection attempts

// Function prototypes for file transmission operations.
void send_file(int sock, const char *filename);
void receive_file(int sock, const char *filename);

int main() {
    int sock;
    struct sockaddr_in server_addr;
    char buffer[BUFSIZE], recv_buf[BUFSIZE];

    // Create a TCP socket.
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Client: socket");
        exit(1);
    }

    // Set up the server address structure.
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    

    // Connect to the main server.
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Client: connect");
        close(sock);
        exit(1);
    }

    printf("\n Connected to S1 Server (Distributed File System)\n");
    printf(" Welcome to COMP-8567 DFS Client Interface\n");

    // Main input loop: Read commands from the user and process them.
    while (1) {
        // Display prompt.
        printf("\nw25clients$ ");
        fflush(stdout);

        // Clear and read the user command.
        memset(buffer, 0, BUFSIZE);
        fgets(buffer, BUFSIZE, stdin);
        // Remove the newline character from input.
        buffer[strcspn(buffer, "\n")] = 0;

        // If command is "exit", break the loop.
        if (strcmp(buffer, "exit") == 0) break;

        // Process the "uploadf" command: upload a file to the distributed file system.
        if (strncmp(buffer, "uploadf ", 8) == 0) {
            char filename[256], destpath[512];
            // Expecting syntax: uploadf <filename> <~S1/path>
            if (sscanf(buffer, "uploadf %s %s", filename, destpath) != 2) {
                printf("Invalid syntax. Use: uploadf <filename> <~S1/path>\n");
                continue;
            }
            // Check if the file exists locally.
            FILE *fp = fopen(filename, "rb");
            if (!fp) {
                printf("File not found locally.\n");
                continue;
            }
            fclose(fp);
            // Send the entire command to the server.
            send(sock, buffer, strlen(buffer), 0);
            // Call send_file to transmit file data.
            send_file(sock, filename);

            // Wait for server acknowledgment.
            int n = recv(sock, recv_buf, BUFSIZE, 0);
            if (n > 0) {
                recv_buf[n] = '\0';
                printf("%s", recv_buf);
            }
        }
        // Process the "downlf" command: download an individual file.
        else if (strncmp(buffer, "downlf ", 7) == 0) {
            char filepath[512];
            // Expecting syntax: downlf <~S1/path/file.ext>
            if (sscanf(buffer, "downlf %s", filepath) != 1) {
                printf("Invalid syntax. Use: downlf <~S1/path/file.ext>\n");
                continue;
            }
            // Send the download request to the server.
            send(sock, buffer, strlen(buffer), 0);
            // Prepare a copy of the filepath and determine a local filename using basename.
            char filepath_copy[512];
            strncpy(filepath_copy, filepath, sizeof(filepath_copy));
            filepath_copy[sizeof(filepath_copy)-1] = '\0';

            char *local_filename = basename(filepath_copy);
            // Receive the file from server and store it locally with the determined name.
            receive_file(sock, local_filename);
        }
        // Process the "downltar" command: download a tar archive of specific file types.
        else if (strncmp(buffer, "downltar ", 9) == 0) {
            char filetype[10];
            // Expecting syntax: downltar <.c|.pdf|.txt>
            if (sscanf(buffer, "downltar %s", filetype) != 1) {
                printf("Invalid syntax. Use: downltar <.c|.pdf|.txt>\n");
                continue;
            }
            // Send tar download request to server.
            send(sock, buffer, strlen(buffer), 0);
            // Determine the expected tar archive name based on file type.
            char tar_filename[64];
            if (strcmp(filetype, ".c") == 0)
                strcpy(tar_filename, "cfiles.tar");
            else if (strcmp(filetype, ".pdf") == 0)
                strcpy(tar_filename, "pdf.tar");
            else if (strcmp(filetype, ".txt") == 0)
                strcpy(tar_filename, "text.tar");
            else {
                printf("Unsupported file type for tar download.\n");
                continue;
            }
            // Receive the tar file from the server.
            receive_file(sock, tar_filename);
        }
        // Process "removef" and "dispfnames" commands: forward as-is and display server responses.
        else if (strncmp(buffer, "removef ", 8) == 0 ||
                 strncmp(buffer, "dispfnames ", 11) == 0) {
            send(sock, buffer, strlen(buffer), 0);
            memset(recv_buf, 0, BUFSIZE);
            int n = recv(sock, recv_buf, BUFSIZE, 0);
            if (n > 0) {
                recv_buf[n] = '\0';
                printf("%s", recv_buf);
            }
        }
        // Handle unknown commands.
        else {
            printf("Unknown command.\n");
        }
    }
    // Close the socket and terminate the client.
    close(sock);
    printf("Client disconnected from S1. Goodbye!\n");
    return 0;
}

// send_file: Reads a file from the local filesystem and transmits its contents to the server.
// It first sends the file size and then streams the file in chunks.
void send_file(int sock, const char *filename) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        perror("fopen error");
        return;
    }
    // Determine file size.
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    // Send the file size.
    send(sock, &fsize, sizeof(long), 0);

    char buffer[BUFSIZE];
    int n;
    // Read and send file data in chunks.
    while ((n = fread(buffer, 1, BUFSIZE, fp)) > 0) {
        send(sock, buffer, n, 0);
    }
    fclose(fp);
}

// receive_file: Receives a file from the server and writes it to the local filesystem.
// The function first receives the file size, then reads file data in chunks until completed.
void receive_file(int sock, const char *filename) {
    long fsize;
    int ret = recv(sock, &fsize, sizeof(long), 0);
    if (ret <= 0) {
        printf("Error receiving file size from server.\n");
        return;
    }
    // Check for error indicator (non-positive file size)
    if (fsize <= 0) {
        printf("Error: File not found on server.\n");
        return;
    }
    
    // Open a local file for writing.
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        perror("Error opening file for writing");
        return;
    }
    char buffer[BUFSIZE];
    long received = 0;
    int n;
    // Receive file content until expected size is reached.
    while (received < fsize) {
        n = recv(sock, buffer, BUFSIZE, 0);
        if (n <= 0) {
            printf("Error receiving file data from server.\n");
            break;
        }
        fwrite(buffer, 1, n, fp);
        received += n;
    }
    fclose(fp);
    // Check if the entire file was received successfully.
    if (received == fsize)
        printf("File downloaded '%s'\n", filename);
    else {
        printf("File download incomplete. Removing incomplete file.\n");
        remove(filename);
    }
}
