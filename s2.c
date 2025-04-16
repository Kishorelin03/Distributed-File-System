// S2.c - PDF Backend Server
// This server is responsible for handling PDF file operations.
// It supports uploading, downloading, deletion, creating tar archives, 
// and listing available PDF files in the designated storage location.
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

#define PORT 7100
#define BUFSIZE 1024
 

// Helper function to reliably obtain the HOME directory.
// It checks the HOME environment variable first; if not set, it retrieves 
// the home directory from the system's password database.
char* get_home_dir() {
    char *home = getenv("HOME");
    if (!home) {
        struct passwd *pw = getpwuid(getuid());
        home = pw->pw_dir;
    }
    return home;
}

// Function prototypes for handling client commands and file operations.
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

    // Create a TCP socket.
    server_sock = socket(AF_INET, SOCK_STREAM, 0);

    // Configure the server address using IPv4 and the defined port.
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // Zero out the rest of the structure.
    memset(&(server_addr.sin_zero), 0, 8);

    // Bind the socket to the port and interface.
    bind(server_sock, (struct sockaddr *)&server_addr, sizeof(struct sockaddr));

    // Listen for incoming connections; allow a backlog of 10 pending connections.
    listen(server_sock, 10);
    printf("📚 S2 Server (PDF) listening on port %d...\n", PORT);

    // Main loop to continuously accept and process client connections.
    while (1) {
        client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &sin_size);
        if (client_sock > 0) {
            // Process the client request in a blocking manner.
            handle_client(client_sock);
            close(client_sock);
        }
    }

    return 0;
}

// handle_client: Receives a command from the connected client and routes it 
// to the proper file operation based on the command prefix.
void handle_client(int sock) {
    char buffer[BUFSIZE] = {0};
    // Read the client command into buffer.
    recv(sock, buffer, sizeof(buffer), 0);

    // Check the command prefix and call the appropriate handler:
    if (strncmp(buffer, "uploadf ", 8) == 0) {
        char filepath[512];
        // Extract the file path argument.
        sscanf(buffer, "uploadf %s", filepath);
        // Remove the '~' prefix and pass the relative path to save_file.
        save_file(sock, filepath + 1);
    }
    else if (strncmp(buffer, "downlf ", 7) == 0) {
        char filepath[512];
        sscanf(buffer, "downlf %s", filepath);
        // Remove the '~' prefix and send the file to the client.
        send_file(sock, filepath + 1);
    }
    else if (strncmp(buffer, "removef ", 8) == 0) {
        char filepath[512];
        sscanf(buffer, "removef %s", filepath);
        // Remove the '~' prefix and call delete_file.
        delete_file(sock, filepath + 1);
    }
    else if (strncmp(buffer, "downltar ", 9) == 0) {
        // Request to download a tar archive containing PDF files.
        send_tar(sock);
    }
    else if (strncmp(buffer, "dispfnames ", 11) == 0) {
        char path[512];
        sscanf(buffer, "dispfnames %s", path);
        // List all PDF files in the specified directory.
        list_files(sock, path + 1);
    }
}

// save_file: Receives a PDF file from the client and stores it locally.
// The function first receives the file size, constructs the full path using the HOME directory,
// creates any necessary parent directories, then writes the file data to disk.
void save_file(int sock, const char *path) {
    long fsize;
    // Receive the file size from the client.
    if (recv(sock, &fsize, sizeof(long), 0) <= 0) {
        perror("Failed to receive file size");
        return;
    }

    char *home = get_home_dir();
    char full_path[BUFSIZE];
    // Build absolute path: $HOME/S2/...
    snprintf(full_path, sizeof(full_path), "%s/%s", home, path);

    // Create parent directories if they do not exist.
    char dir[BUFSIZE];
    strncpy(dir, full_path, BUFSIZE);
    dir[BUFSIZE - 1] = '\0';
    // Find the last '/' to isolate the directory part of the path.
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        char cmd[BUFSIZE];
        snprintf(cmd, sizeof(cmd), "mkdir -p %s", cmd); // Note: We use our computed dir.
        // Build and execute the command to create directories recursively.
        snprintf(cmd, sizeof(cmd), "mkdir -p %s", dir);
        system(cmd);
    }

    // Open the file for binary writing.
    FILE *fp = fopen(full_path, "wb");
    if (!fp) {
        perror("❌ fopen in S2 (PDF) failed");
        return;
    }
    char buf[BUFSIZE];
    long received = 0;
    int n;
    // Continue receiving data until the entire file is written.
    while (received < fsize) {
        n = recv(sock, buf, BUFSIZE, 0);
        fwrite(buf, 1, n, fp);
        received += n;
    }
    fclose(fp);
    printf("📥 Stored: %s\n", full_path);
}


// send_file: Sends a PDF file to the client.
// Constructs the full file path, reads the file, sends its size first,
// then streams the file content to the client.
void send_file(int sock, const char *path) {
    char *home = get_home_dir();
    char full_path[BUFSIZE];
    // Construct absolute path: $HOME/S2/...
    snprintf(full_path, sizeof(full_path), "%s/%s", home, path);

    FILE *fp = fopen(full_path, "rb");
    if (!fp) {
        // If file not found, send a zero file size to indicate an error.
        long zero = 0;
        send(sock, &zero, sizeof(long), 0);
        return;
    }
    // Determine file size.
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    // Send the size first.
    send(sock, &fsize, sizeof(long), 0);
    char buf[BUFSIZE];
    int n;
    // Stream file in chunks.
    while ((n = fread(buf, 1, BUFSIZE, fp)) > 0) {
        send(sock, buf, n, 0);
    }
    fclose(fp);
    printf("📤 Sent file: %s\n", full_path);
}

// delete_file: Deletes the specified PDF file from the server's storage.
// Constructs the absolute file path and attempts to remove it.
// Sends a confirmation message upon success or an error message if the file is not found.
void delete_file(int sock, const char *path) {
    char *home = get_home_dir();
    char full_path[BUFSIZE];
    snprintf(full_path, sizeof(full_path), "%s/%s", home, path);
    if (remove(full_path) == 0) {
        char *msg = "✅ File removed.\n";
        send(sock, msg, strlen(msg), 0);
    } else {
        char *msg = "❌ File not found.\n";
        send(sock, msg, strlen(msg), 0);
    }
}

// send_tar: Creates a tar archive containing all PDF files stored under $HOME/S2,
// then sends the archive to the client.
// It uses a system command to generate the tar archive, streams the file, and cleans up afterward.
void send_tar(int sock) {
    char *home = get_home_dir();
    char tmpTar[BUFSIZE];
    char tmpList[BUFSIZE];
    char tar_cmd[BUFSIZE];

    // Create temporary file names in your home directory.
    snprintf(tmpTar, sizeof(tmpTar), "%s/pdffiles.tar", home);
    snprintf(tmpList, sizeof(tmpList), "%s/pdffiles.list", home);

    // Remove existing temporary files, change to the S2 directory,
    // generate a list of all PDF files, and create a tar archive from that list.
    snprintf(tar_cmd, sizeof(tar_cmd),
             "rm -f %s; rm -f %s; cd %s/S2 && find . -type f -name \"*.pdf\" > %s && tar -cf %s -T %s",
             tmpTar, tmpList, home, tmpList, tmpTar, tmpList);
    system(tar_cmd);

    // Open the newly created tar archive.
    FILE *fp = fopen(tmpTar, "rb");
    if (!fp) {
        long zero = 0;
        send(sock, &zero, sizeof(long), 0);
        return;
    }

    // Determine the size of the archive.
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    // Send the size first.
    send(sock, &fsize, sizeof(long), 0);

    // Stream the tar file's contents.
    char buf[BUFSIZE];
    int n;
    while ((n = fread(buf, 1, BUFSIZE, fp)) > 0) {
        send(sock, buf, n, 0);
    }
    fclose(fp);

    // Remove the temporary tar and list files.
    remove(tmpTar);
    remove(tmpList);

    printf("📦 Sent tar file: %s\n", tmpTar);
}

// list_files: Lists all PDF files in the specified directory under $HOME/S2.
// It opens the directory, filters regular files with a ".pdf" extension,
// aggregates the file names, and sends the result to the client.
void list_files(int sock, const char *dirpath) {
    char *home = get_home_dir();
    char full_dir[BUFSIZE];
    // Construct the full directory path.
    snprintf(full_dir, sizeof(full_dir), "%s/%s", home, dirpath);
    DIR *dir = opendir(full_dir);
    if (!dir) {
        return;
    }
    struct dirent *entry;
    char result[BUFSIZE] = "";
    // Iterate through directory entries.
    while ((entry = readdir(dir)) != NULL) {
        // Process only regular files.
        if (entry->d_type == DT_REG) {
            const char *ext = strrchr(entry->d_name, '.');
            // Check if the file has a ".pdf" extension.
            if (ext && strcmp(ext, ".pdf") == 0) {
                strcat(result, entry->d_name);
                strcat(result, "\n");
            }
        }
    }
    closedir(dir);
    
    send(sock, result, strlen(result), 0);
}
