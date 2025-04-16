// S3.c - TXT Backend Server
// This server listens on TCP port 6200 and handles operations related to text files (.txt).
// It supports uploading, downloading, deleting text files, creating a tar archive of text files,
// and listing available text files in the server's storage.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pwd.h>

#define PORT 7200
#define BUFSIZE 1024

// Helper function to reliably retrieve the HOME directory.
// It first attempts to obtain the HOME environment variable, and if that's not available,
// it retrieves the user's home directory from the system's password database.
char* get_home_dir() {
    char *home = getenv("HOME");
    if (!home) {
        struct passwd *pw = getpwuid(getuid());
        home = pw->pw_dir;
    }
    return home;
}

// Function prototypes for handling client requests and file operations.
void handle_client(int);
void save_file(int, const char*);
void send_file(int, const char*);
void delete_file(int, const char*);
void send_tar(int);
void list_files(int, const char*);

int main() {
    int server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t sin_size = sizeof(struct sockaddr_in);

    // Create a TCP socket using IPv4.
    server_sock = socket(AF_INET, SOCK_STREAM, 0);

    // Configure the server address structure.
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    memset(&(server_addr.sin_zero), 0, 8);

    // Bind the socket to the specified port and IP address.
    bind(server_sock, (struct sockaddr *)&server_addr, sizeof(struct sockaddr));

    // Start listening for incoming connections; allow up to 10 pending connections.
    listen(server_sock, 10);
    printf("S3 Server (TXT) listening on port %d...\n", PORT);

    // Main loop: continuously accept and process client connections.
    while (1) {
        client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &sin_size);
        if (client_sock > 0) {
            handle_client(client_sock);
            close(client_sock);
        }
    }

    return 0;
}

// Processes a single client connection by reading the client's command and routing
// it to the appropriate file operation function.
void handle_client(int sock) {
    char buffer[BUFSIZE] = {0};
    // Read the command sent by the client.
    recv(sock, buffer, sizeof(buffer), 0);

    // Check for the "uploadf" command to upload a file.
    if (strncmp(buffer, "uploadf ", 8) == 0) {
        char filepath[512];
        sscanf(buffer, "uploadf %s", filepath);
        // Remove the '~' prefix and call save_file to store the file.
        save_file(sock, filepath + 1);
    }
    // Check for the "downlf" command to download a file.
    else if (strncmp(buffer, "downlf ", 7) == 0) {
        char filepath[512];
        sscanf(buffer, "downlf %s", filepath);
        // Remove the '~' prefix and call send_file to send the file to the client.
        send_file(sock, filepath + 1);
    }
    // Check for the "removef" command to delete a file.
    else if (strncmp(buffer, "removef ", 8) == 0) {
        char filepath[512];
        sscanf(buffer, "removef %s", filepath);
        // Remove the '~' prefix and call delete_file to remove the file.
        delete_file(sock, filepath + 1);
    }
    // Check for the "downltar" command to request a tar archive containing all TXT files.
    else if (strncmp(buffer, "downltar ", 9) == 0) {
        send_tar(sock);
    }
    // Check for the "dispfnames" command to list TXT file names in a given directory.
    else if (strncmp(buffer, "dispfnames ", 11) == 0) {
        char path[512];
        sscanf(buffer, "dispfnames %s", path);
        // Remove the '~' prefix and call list_files to send the list back to the client.
        list_files(sock, path + 1);
    }
}

// Stores an uploaded text file sent by the client.
// The function first receives the size of the file, constructs an absolute file path
// (under the user's HOME directory) and creates any required directories before writing
// the file to disk in binary mode.
void save_file(int sock, const char *path) {
    long fsize;
    // Receive the file size.
    recv(sock, &fsize, sizeof(long), 0);

    char *home = get_home_dir();
    // Build absolute file path under $HOME/S3
    char full_path[BUFSIZE];
    snprintf(full_path, sizeof(full_path), "%s/%s", home, path);

    // Create necessary parent directories if they do not exist.
    char dir[BUFSIZE];
    strncpy(dir, full_path, BUFSIZE);
    dir[BUFSIZE - 1] = '\0';
    char *last_slash = strrchr(dir, '/');
    if (last_slash != NULL) {
        *last_slash = '\0';
        char cmd[BUFSIZE];
        snprintf(cmd, sizeof(cmd), "mkdir -p %s", dir);
        system(cmd);
    }

    // Open the file in binary write mode.
    FILE *fp = fopen(full_path, "wb");
    if (!fp) {
        perror("fopen failed");
        return;
    }
    char buf[BUFSIZE];
    long received = 0;
    int n;

    // Receive file data in chunks until the entire file is received.
    while (received < fsize) {
        n = recv(sock, buf, BUFSIZE, 0);
        fwrite(buf, 1, n, fp);
        received += n;
    }
    fclose(fp);
    printf("Stored TXT: %s\n", full_path);
}

// Sends the requested text file to the client.
// It constructs the file's absolute path, reads the file size, sends that first,
// and then streams the file content in chunks.
void send_file(int sock, const char *path) {
    char *home = get_home_dir();
    char full_path[BUFSIZE];
    snprintf(full_path, sizeof(full_path), "%s/%s", home, path);

    FILE *fp = fopen(full_path, "rb");
    
    if (!fp) {
        // If the file does not exist, send a zero size to indicate the error.
        long zero = 0;
        send(sock, &zero, sizeof(long), 0);
        return;
    }

    // Determine the file size by seeking to the end.
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    // Send the file size to the client.
    send(sock, &fsize, sizeof(long), 0);

    char buf[BUFSIZE];
    int n;
    // Stream the file content in chunks.
    while ((n = fread(buf, 1, BUFSIZE, fp)) > 0) {
        send(sock, buf, n, 0);
    }
    fclose(fp);
    printf("Sent TXT file: %s\n", full_path);
}

// Deletes the specified file from the server.
// It builds the file's absolute path and attempts to remove it from disk.
// A confirmation message is sent back to the client indicating success or failure.
void delete_file(int sock, const char *path) {
    char *home = get_home_dir();
    char full_path[BUFSIZE];
    snprintf(full_path, sizeof(full_path), "%s/%s", home, path);
    if (remove(full_path) == 0) {
        char *msg = "File removed.\n";
        send(sock, msg, strlen(msg), 0);
    } else {
        char *msg = "File not found.\n";
        send(sock, msg, strlen(msg), 0);
    }
}

// Creates a tar archive containing all text files (.txt) under the $HOME/S3 directory
// and sends it to the client.
// It uses a system command to find and pack the files, then streams the resulting tar file.
void send_tar(int sock) {
    char *home = get_home_dir();
    char tmpTar[BUFSIZE];
    char tmpList[BUFSIZE];
    char tar_cmd[BUFSIZE];

    // Build temporary file names based on the HOME directory.
    snprintf(tmpTar, sizeof(tmpTar), "%s/textfiles.tar", home);
    snprintf(tmpList, sizeof(tmpList), "%s/textfiles.list", home);

    // Remove any existing temporary files, change to $HOME/S3,
    // list all .txt files, and create a tar archive from that list.
    snprintf(tar_cmd, sizeof(tar_cmd),
             "rm -f %s; rm -f %s; cd %s/S3 && find . -type f -name \"*.txt\" > %s && tar -cf %s -T %s",
             tmpTar, tmpList, home, tmpList, tmpTar, tmpList);
    system(tar_cmd);

    // Open the created tar archive from the temporary file.
    FILE *fp = fopen(tmpTar, "rb");
    if (!fp) {
        long zero = 0;
        send(sock, &zero, sizeof(long), 0);
        return;
    }

    // Determine the size of the tar archive.
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    // Send the tar archive size first.
    send(sock, &fsize, sizeof(long), 0);

    // Stream the tar file content to the client.
    char buf[BUFSIZE];
    int n;
    while ((n = fread(buf, 1, BUFSIZE, fp)) > 0) {
        send(sock, buf, n, 0);
    }
    fclose(fp);

    // Clean up the temporary files.
    remove(tmpTar);
    remove(tmpList);

    printf("Sent tar file: %s\n", tmpTar);
}

// Lists all text files (.txt) in a specified directory under $HOME.
// The function gathers the filenames and sends a newline-separated list to the client.
void list_files(int sock, const char *dirpath) {
    char *home = get_home_dir();
    char full_dir[BUFSIZE];
    snprintf(full_dir, sizeof(full_dir), "%s/%s", home, dirpath);
    DIR *dir = opendir(full_dir);
    if (!dir) {
        return;
    }
    struct dirent *entry;
    char result[BUFSIZE] = "";
    // Iterate over the directory entries.
    while ((entry = readdir(dir)) != NULL) {
        // Process only regular files.
        if (entry->d_type == DT_REG) {
            const char *ext = strrchr(entry->d_name, '.');
            // Check if the file has a ".txt" extension.
            if (ext && strcmp(ext, ".txt") == 0) {
                strcat(result, entry->d_name);
                strcat(result, "\n");
            }
        }
    }
    closedir(dir);
    // Send the aggregated list of file names to the client.
    send(sock, result, strlen(result), 0);
}
