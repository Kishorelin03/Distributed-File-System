// S4.c - ZIP Backend Server
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

#define PORT 7300
#define BUFSIZE 1024

// Helper function to reliably retrieve the HOME directory.
// It first attempts to retrieve the HOME environment variable.
// If that's not available, it uses the passwd structure.

char* get_home_dir() {
    char *home = getenv("HOME");
    if (!home) {
        struct passwd *pw = getpwuid(getuid());
        home = pw->pw_dir;
    }
    return home;
}

// Function prototypes for client handling and file-related operations.

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
    // Create a socket using IPv4 and TCP.
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    // Set up the server address structure.
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    memset(&(server_addr.sin_zero), 0, 8);

    // Bind the socket to the specified port and address.
    bind(server_sock, (struct sockaddr *)&server_addr, sizeof(struct sockaddr));

    // Listen for incoming connections; allow up to 10 pending connections.
    listen(server_sock, 10);
    printf("S4 Server (ZIP) listening on port %d...\n", PORT);

    // Main loop: accept and handle incoming client connections.
    while (1) {
        client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &sin_size);
        if (client_sock > 0) {
            handle_client(client_sock);
            close(client_sock);
        }
    }

    return 0;
}

// Handles the communication with a connected client.
void handle_client(int sock) {
    char buffer[BUFSIZE] = {0};

    // Receive the command from the client.
    recv(sock, buffer, sizeof(buffer), 0);

    // Determine the command sent by the client by checking the prefix of the message.
    if (strncmp(buffer, "uploadf ", 8) == 0) {
        char filepath[512];

        // Extract the file path from the command.
        sscanf(buffer, "uploadf %s", filepath);
        // Call save_file() with the path starting after the '~' character.
        save_file(sock, filepath + 1);
    }
    else if (strncmp(buffer, "downlf ", 7) == 0) {
        char filepath[512];
        sscanf(buffer, "downlf %s", filepath);
        // Call send_file() with the path starting after the '~' character.
        send_file(sock, filepath + 1);
    }
    else if (strncmp(buffer, "removef ", 8) == 0) {
        char filepath[512];
        sscanf(buffer, "removef %s", filepath);
        // Call delete_file() with the path starting after the '~' character.
        delete_file(sock, filepath + 1);
    }
    else if (strncmp(buffer, "downltar ", 9) == 0) {
        // Client requests a tar archive of all .zip files.
        send_tar(sock);
    }
    else if (strncmp(buffer, "dispfnames ", 11) == 0) {
        char path[512];
        sscanf(buffer, "dispfnames %s", path);
        // List all .zip files in the given directory (after the '~' character).
        list_files(sock, path + 1);
    }
}

// Saves an uploaded file from the client to the server's file system.
void save_file(int sock, const char *path) {
    long fsize;
    // Receive the file size from the client.
    recv(sock, &fsize, sizeof(long), 0);

    char *home = get_home_dir();
    // Construct the absolute file path under $HOME/S4 directory.
    char full_path[BUFSIZE];
    snprintf(full_path, sizeof(full_path), "%s/%s", home, path);

    // Create necessary parent directories if they do not exist.
    char dir[BUFSIZE];
    strncpy(dir, full_path, BUFSIZE);
    dir[BUFSIZE - 1] = '\0';
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        char cmd[BUFSIZE];
        // Command to create directories recursively.
        snprintf(cmd, sizeof(cmd), "mkdir -p %s", dir);
        system(cmd);
    }

    // Open the file in binary write mode.
    FILE *fp = fopen(full_path, "wb");
    if (!fp) {
        perror("fopen in S4");
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
    printf("Stored ZIP: %s\n", full_path);
}


// Sends the requested file to the client.
void send_file(int sock, const char *path) {
    char *home = get_home_dir();
    // Construct the absolute file path under $HOME.
    char full_path[BUFSIZE];
    snprintf(full_path, sizeof(full_path), "%s/%s", home, path);

    FILE *fp = fopen(full_path, "rb");
    if (!fp) {
        // If the file is not found, send a zero file size to inform the client.
        long zero = 0;
        send(sock, &zero, sizeof(long), 0);
        return;
    }

    // Determine the file size.
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    // Send the file size first.
    send(sock, &fsize, sizeof(long), 0);
    char buf[BUFSIZE];
    int n;
    // Send file data in chunks.
    while ((n = fread(buf, 1, BUFSIZE, fp)) > 0) {
        send(sock, buf, n, 0);
    }
    fclose(fp);
    printf("Sent file: %s\n", full_path);
}

// Deletes a specified file from the server's file system and informs the client of the result.
void delete_file(int sock, const char *path) {
    char *home = get_home_dir();
    // Build the absolute path for the file.
    char full_path[BUFSIZE];
    snprintf(full_path, sizeof(full_path), "%s/%s", home, path);
    // Attempt to delete the file.
    if (remove(full_path) == 0) {
        char *msg = "File removed.\n";
        send(sock, msg, strlen(msg), 0);
    } else {
        char *msg = "File not found.\n";
        send(sock, msg, strlen(msg), 0);
    }
}

// Packages all .zip files under the $HOME/S4 directory into a tar archive and sends it to the client.
void send_tar(int sock) {
    char *home = get_home_dir();
    // Change directory to $HOME/S4 and create a tar archive of all .zip files found.
    char cmd[BUFSIZE];
    snprintf(cmd, sizeof(cmd), "cd %s/S4 && find . -type f -name \"*.zip\" | tar -cf /tmp/zip.tar -T -", home);
    system(cmd);
    
    FILE *fp = fopen("/tmp/zip.tar", "rb");
    if (!fp) {
        // If tar file cannot be opened, indicate failure to the client by sending a zero file size.
        long zero = 0;
        send(sock, &zero, sizeof(long), 0);
        return;
    }
    // Determine the tar archive file size.
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    // Send the archive size first.
    send(sock, &fsize, sizeof(long), 0);
    char buf[BUFSIZE];
    int n;
    // Send tar archive contents in chunks.
    while ((n = fread(buf, 1, BUFSIZE, fp)) > 0) {
        send(sock, buf, n, 0);
    }
    fclose(fp);
    // Remove the temporary tar file.
    remove("/tmp/zip.tar");
    printf("Sent tar: zip.tar\n");
}

// Lists all files in a given directory under $HOME that have a .zip extension.
// Sends the list of filenames (each separated by a newline) back to the client.
void list_files(int sock, const char *dirpath) {
    char *home = get_home_dir();
    // Construct the full directory path.
    char full_dir[BUFSIZE];
    snprintf(full_dir, sizeof(full_dir), "%s/%s", home, dirpath);
    DIR *dir = opendir(full_dir);
    if (!dir) {
        // In case the directory does not exist, simply return.
        return;
    }
    struct dirent *entry;
    char result[BUFSIZE] = "";
    // Iterate over all entries in the directory.
    while ((entry = readdir(dir)) != NULL) {
        // Check if the entry is a regular file.
        if (entry->d_type == DT_REG) {
            // Look for files with a ".zip" extension.
            const char *ext = strrchr(entry->d_name, '.');
            if (ext && strcmp(ext, ".zip") == 0) {
                // Append the filename and a newline to the result.
                strcat(result, entry->d_name);
                strcat(result, "\n");
            }
        }
    }
    closedir(dir);
    // Send the list of filenames back to the client.
    send(sock, result, strlen(result), 0);
}
