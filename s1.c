// S1.c - Main Server for COMP8567 Distributed File System
// This server acts as the main hub for handling file operations in a distributed file system.
// It accepts connections from clients and routes commands (upload, download, remove, etc.) to the appropriate handlers,
// which may process the file locally (for .c files) or forward the request to dedicated backend servers for other file types.#include <stdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/time.h>
#include <netinet/tcp.h> 
#include <pwd.h>  // For getpwuid

#define PORT 7010
#define BACKLOG 10
#define BUFSIZE 1024

// Helper function to get the HOME directory reliably.
// It first checks the environment variable "HOME", and if not found, falls back to system information.
char* get_home_dir() {
    char *home = getenv("HOME");
    if (home == NULL) {
        struct passwd *pw = getpwuid(getuid());
        home = pw->pw_dir;
    }
    return home;
}

// Function prototypes for handling client operations and file forwarding.
void prcclient(int client_sock);
void handle_upload(int, char*);
void forward_file(const char*, const char*, int);
void handle_download(int, char*);
void handle_remove(int, char*);
void handle_downltar(int, char*);
void handle_dispfnames(int, char*);
int collect_files_from_server(const char *path, int port, char *buffer);

// Main function: sets up the server socket, accepts client connections,
// forks a new process for each client, and calls prcclient() to process commands.
int main() {
    int server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t sin_size;
    pid_t pid;

    // Create socket
    if ((server_sock = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("S1: socket");
        exit(1);
    }

    // Setup the server address structure.
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    memset(&(server_addr.sin_zero), 0, 8);

    // Bind the socket to the specified port and interface.
    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(struct sockaddr)) == -1) {
        perror("S1: bind");
        close(server_sock);
        exit(1);
    }

    // Listen for incoming connections.
    if (listen(server_sock, BACKLOG) == -1) {
        perror("S1: listen");
        exit(1);
    }

    printf("\n S1 Main Server started. Listening on port %d...\n", PORT);

    // Main loop to accept incoming client connections.
    while (1) {
        sin_size = sizeof(struct sockaddr_in);
        client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &sin_size);
        if (client_sock == -1) {
            perror("S1: accept");
            continue;
        }

        printf(" New client connected.\n");

        // Fork a new process to handle the client connection.
        if ((pid = fork()) == 0) {
            close(server_sock); // Child process closes listening socket.
            prcclient(client_sock); // Process client commands.
            close(client_sock);
            exit(0); // Terminate child process.
        }


        close(client_sock); // Parent process closes connected socket.
        while (waitpid(-1, NULL, WNOHANG) > 0); // Reap any zombie processes.
    }

    return 0;
}


// prcclient: Processes commands from a connected client in a loop.
// It receives commands and dispatches them to the appropriate handler function.
void prcclient(int client_sock) {
    char buffer[BUFSIZE];

    while (1) {
        memset(buffer, 0, BUFSIZE);
        int bytes = recv(client_sock, buffer, BUFSIZE - 1, 0);
        if (bytes <= 0) {
            printf("Client disconnected.\n");
            break;
        }
        buffer[bytes] = '\0';
        printf("Command received: %s\n", buffer);
        
        // Route the command to the corresponding handler based on its prefix.
        if (strncmp(buffer, "uploadf ", 8) == 0) {
            handle_upload(client_sock, buffer);
        }
        else if (strncmp(buffer, "downlf ", 7) == 0) {
            handle_download(client_sock, buffer);
        }
        else if (strncmp(buffer, "removef ", 8) == 0) {
            handle_remove(client_sock, buffer);
        }
        else if (strncmp(buffer, "downltar ", 9) == 0) {
            handle_downltar(client_sock, buffer);
        }
        else if (strncmp(buffer, "dispfnames ", 11) == 0) {
            handle_dispfnames(client_sock, buffer);
        }
        else {
            char *msg = "Invalid command.\n";
            send(client_sock, msg, strlen(msg), 0);
        }
    }
}


// create_directories: Creates all necessary parent directories for the given file path.
// It extracts the directory part of the full path and executes a system command to create it.

void create_directories(const char* full_path) {
    char path[BUFSIZE];
    strcpy(path, full_path);
    char *p = strrchr(path, '/');
    if (p)
        *p = '\0';
    char cmd[BUFSIZE];
    snprintf(cmd, sizeof(cmd), "mkdir -p %s", path);
    system(cmd);
}

// handle_upload: Processes an upload command.
// It expects a command string containing the filename and destination path.
// Files ending with .c are stored locally; other types are temporarily saved and then forwarded to a backend.
void handle_upload(int client_sock, char *cmd) {
    char filename[256], dest_path[512];
    sscanf(cmd, "uploadf %s %s", filename, dest_path);

    // Reject if destination does not start with the expected marker "~S1".
    if (strncmp(dest_path, "~S1", 3) != 0) {
            char *msg = "Destination must start with ~S1.\n";
            send(client_sock, msg, strlen(msg), 0);
            return;
        }

    // Receive the file size from the client.
    long filesize;
    recv(client_sock, &filesize, sizeof(long), 0);

    const char *ext = strrchr(filename, '.');
    char *home = get_home_dir();

    // For .c files, store directly in S1's directory.
    if (ext && strcmp(ext, ".c") == 0) {
        char save_path[BUFSIZE];
        // Construct the full path: $HOME/S1/...
        // dest_path+1 converts "~S1/folder" to "S1/folder".
        snprintf(save_path, sizeof(save_path), "%s/%s/%s", home, dest_path + 1, filename);
        create_directories(save_path);
        printf("Trying to save (S1): %s\n", save_path);

        FILE *fp = fopen(save_path, "wb");
        if (!fp) {
            perror("fopen failed in S1 for .c file");
            char *msg = "Failed to save file.\n";
            send(client_sock, msg, strlen(msg), 0);
            return;
        }
        char buffer[BUFSIZE];
        long received = 0;
        // Receive file data and write to file until the full file is received.
        while (received < filesize) {
            int n = recv(client_sock, buffer, BUFSIZE, 0);
            fwrite(buffer, 1, n, fp);
            received += n;
        }
        fclose(fp);
        char *msg = "File stored successfully.\n";
        send(client_sock, msg, strlen(msg), 0);
        return;
    } else {
        // For non-.c files, save temporarily then forward to backend.
        char temp_path[BUFSIZE];
        snprintf(temp_path, sizeof(temp_path), "/tmp/forwarded_%d_%s", getpid(), filename);
        printf("Writing non-.c file to temporary path: %s\n", temp_path);

        FILE *fp = fopen(temp_path, "wb");
        if (!fp) {
            perror("fopen failed in /tmp for non-.c file");
            char *msg = "Failed to save file to temporary location.\n";
            send(client_sock, msg, strlen(msg), 0);
            return;
        }
        char buffer[BUFSIZE];
        long received = 0;
        while (received < filesize) {
            int n = recv(client_sock, buffer, BUFSIZE, 0);
            fwrite(buffer, 1, n, fp);
            received += n;
        }
        fclose(fp);

        int port = 0;
        // Determine the backend server's port based on the file extension.
        if (ext && strcmp(ext, ".pdf") == 0)
            port = 7100;
        else if (ext && strcmp(ext, ".txt") == 0)
            port = 7200;
        else if (ext && strcmp(ext, ".zip") == 0)
            port = 7300;
        else {
            char *msg = "Unsupported file type.\n";
            send(client_sock, msg, strlen(msg), 0);
            remove(temp_path);
            return;
        }
    
        char target_path[BUFSIZE];
        // Construct the target path for backend storage.
        // The transformation converts "~S1/..." to "~S(backend)/...".
        snprintf(target_path, sizeof(target_path), "~S%d%s/%s",
                 port == 7100 ? 2 : port == 7200 ? 3 : 4,
                 dest_path + 3, filename);
        printf("➡ Forwarding file from %s to backend (target: %s, port: %d)\n", temp_path, target_path, port);
        forward_file(temp_path, target_path, port);
        remove(temp_path);
        char *msg = "File stored successfully.\n";
        send(client_sock, msg, strlen(msg), 0);
    }
}

// forward_file: Establishes a connection to the designated backend server and forwards the file.
// It sends an "uploadf" command along with the destination path, file size, and file data.
void forward_file(const char *filepath, const char *dest_path, int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in servaddr;
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &servaddr.sin_addr);

    // Connect to the backend server.
    if (connect(sock, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("Forward file connect failed");
        return;
    }
    char cmd[BUFSIZE];
    snprintf(cmd, sizeof(cmd), "uploadf %s", dest_path);
    send(sock, cmd, strlen(cmd), 0);
    usleep(100000);  // Brief pause to allow the backend server to prepare.
    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        perror("Forward file fopen failed");
        close(sock);
        return;
    }
    // Determine the file size and send it.
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    send(sock, &fsize, sizeof(long), 0);
    char buffer[BUFSIZE];
    int bytes;
    // Forward the file data in chunks.
    while ((bytes = fread(buffer, 1, BUFSIZE, fp)) > 0) {
        send(sock, buffer, bytes, 0);
    }
    fclose(fp);
    close(sock);
}

// handle_download: Processes a download request from the client.
// For .c files stored in S1, the file is sent directly. For other file types,
// the request is forwarded to the corresponding backend server.
void handle_download(int client_sock, char *cmd) {
    char filepath[512];
    sscanf(cmd, "downlf %s", filepath);
    const char *ext = strrchr(filepath, '.');
    long err = -1;  // error indicator

    // If file extension is not provided, immediately send error indicator.
    if (!ext) {
        send(client_sock, &err, sizeof(long), 0);
        return;
    }

    char *home = get_home_dir();

    if (strcmp(ext, ".c") == 0) {
        // Build absolute path for .c files stored locally in S1.
        char real_path[512];
        snprintf(real_path, sizeof(real_path), "%s/%s", home, filepath + 1);
        FILE *fp = fopen(real_path, "rb");
        if (!fp) {
            // File not found: send error indicator.
            send(client_sock, &err, sizeof(long), 0);
            return;
        }
        // Determine the file size and send it.
        fseek(fp, 0, SEEK_END);
        long fsize = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        send(client_sock, &fsize, sizeof(long), 0);
        char buffer[BUFSIZE];
        int n;
        // Stream the file data to the client.
        while ((n = fread(buffer, 1, BUFSIZE, fp)) > 0) {
            send(client_sock, buffer, n, 0);
        }
        fclose(fp);
    } else {
        // For non-.c files, forward the request to the appropriate backend server.
        int port = 0;
        char corrected_path[512];
        if (strcmp(ext, ".pdf") == 0) {
            port = 7100;
            snprintf(corrected_path, sizeof(corrected_path), "~S2%s", filepath + 3);
        } else if (strcmp(ext, ".txt") == 0) {
            port = 7200;
            snprintf(corrected_path, sizeof(corrected_path), "~S3%s", filepath + 3);
        } else if (strcmp(ext, ".zip") == 0) {
            port = 7300;
            snprintf(corrected_path, sizeof(corrected_path), "~S4%s", filepath + 3);
        } else {
            // Unsupported file type.
            send(client_sock, &err, sizeof(long), 0);
            return;
        }
        // Connect to the backend server to request the file.
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            send(client_sock, &err, sizeof(long), 0);
            return;
        }
        struct sockaddr_in servaddr;
        servaddr.sin_family = AF_INET;
        servaddr.sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &servaddr.sin_addr);
        if (connect(sock, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
            send(client_sock, &err, sizeof(long), 0);
            close(sock);
            return;
        }
        // Send the download command to the backend.
        char get_cmd[512];
        snprintf(get_cmd, sizeof(get_cmd), "downlf %s", corrected_path);
        send(sock, get_cmd, strlen(get_cmd), 0);
        long fsize = 0;
        if (recv(sock, &fsize, sizeof(long), 0) <= 0) {
            send(client_sock, &err, sizeof(long), 0);
            close(sock);
            return;
        }
        // If the backend returns 0 or negative size, relay the error.
        if (fsize <= 0) {
            send(client_sock, &err, sizeof(long), 0);
            close(sock);
            return;
        }
        // Send the file size to the client then stream the file data.
        send(client_sock, &fsize, sizeof(long), 0);
        char buffer[BUFSIZE];
        long received = 0;
        int n;
        while (received < fsize) {
            n = recv(sock, buffer, BUFSIZE, 0);
            if (n <= 0) {
                break;
            }
            send(client_sock, buffer, n, 0);
            received += n;
        }
        close(sock);
    }
}

// handle_remove: Processes a file removal request.
// For .c files, the removal is handled locally; for other file types,
// the request is forwarded to the appropriate backend server.
void handle_remove(int client_sock, char *cmd) {
    char filepath[512];
    sscanf(cmd, "removef %s", filepath);
    const char *ext = strrchr(filepath, '.');
    if (!ext) {
        char *msg = "Invalid file extension.\n";
        send(client_sock, msg, strlen(msg), 0);
        return;
    }
    char *home = get_home_dir();
    if (strcmp(ext, ".c") == 0) {
        // Remove .c files from local storage.
        char local_path[512];
        snprintf(local_path, sizeof(local_path), "%s/%s", home, filepath + 1);
        if (remove(local_path) == 0) {
            char *msg = "File deleted.\n";
            send(client_sock, msg, strlen(msg), 0);
        } else {
            char *msg = "File not found or cannot delete.\n";
            send(client_sock, msg, strlen(msg), 0);
        }
    } else {
        // Forward removal requests for other file types to the correct backend.
        int port = 0;
        char corrected_path[512];
        if (strcmp(ext, ".pdf") == 0) {
            port = 7100;
            snprintf(corrected_path, sizeof(corrected_path), "~S2%s", filepath + 3);
        } else if (strcmp(ext, ".txt") == 0) {
            port = 7200;
            snprintf(corrected_path, sizeof(corrected_path), "~S3%s", filepath + 3);
        } else if (strcmp(ext, ".zip") == 0) {
            port = 7300;
            snprintf(corrected_path, sizeof(corrected_path), "~S4%s", filepath + 3);
        } else {
            char *msg = "Unsupported file type.\n";
            send(client_sock, msg, strlen(msg), 0);
            return;
        }
        // Connect to the backend server.
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in servaddr;
        servaddr.sin_family = AF_INET;
        servaddr.sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &servaddr.sin_addr);
        if (connect(sock, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
            char *msg = "Cannot connect.\n";
            send(client_sock, msg, strlen(msg), 0);
            return;
        }
        // Send the removal command to the backend.
        char del_cmd[512];
        snprintf(del_cmd, sizeof(del_cmd), "removef %s", corrected_path);
        send(sock, del_cmd, strlen(del_cmd), 0);
        char reply[256] = {0};
        // Relay the reply from the backend to the client.
        recv(sock, reply, sizeof(reply), 0);
        send(client_sock, reply, strlen(reply), 0);
        close(sock);
    }
}

// handle_downltar: Processes a command to create and download a tar archive.
// For .c files, the archive is generated locally; for .pdf and .txt files, the request is forwarded.
// Only .c, .pdf, and .txt file types are supported for tar archiving.
void handle_downltar(int client_sock, char *cmd) {
    char filetype[10];
    sscanf(cmd, "downltar %s", filetype);
    char *home = get_home_dir();

    if (strcmp(filetype, ".c") == 0) {
        char tar_cmd[BUFSIZE];
        char tmpTar[BUFSIZE];
        char tmpList[BUFSIZE];
        // Build paths for temporary tar and list files in the home directory.
        snprintf(tmpTar, sizeof(tmpTar), "%s/cfiles.tar", home);
        snprintf(tmpList, sizeof(tmpList), "%s/cfiles.list", home);
        
        
        // Remove any existing temporary files and create a new tar archive of .c files in S1.
        snprintf(tar_cmd, sizeof(tar_cmd),
                 "rm -f %s; rm -f %s; cd %s/S1 && find . -type f -name \"*.c\" > %s && tar -cf %s -T %s",
                 tmpTar, tmpList, home, tmpList, tmpTar, tmpList);
        system(tar_cmd);
        
        FILE *fp = fopen(tmpTar, "rb");
        if (!fp) {
            char *msg = "Could not create cfiles.tar.\n";
            send(client_sock, msg, strlen(msg), 0);
            return;
        }
        // Determine tar archive size and check for empty archive.
        fseek(fp, 0, SEEK_END);
        long fsize = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        if (fsize == 0) {
            char *msg = "No .c files found to create tar archive.\n";
            send(client_sock, msg, strlen(msg), 0);
            fclose(fp);
            remove(tmpTar);
            remove(tmpList);
            return;
        }
        // Send the tar archive size followed by the archive data.
        send(client_sock, &fsize, sizeof(long), 0);
        char buf[BUFSIZE];
        int n;
        while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
            send(client_sock, buf, n, 0);
        fclose(fp);
        remove(tmpTar);
        remove(tmpList);
        printf("Sent cfiles.tar to client (%ld bytes)\n", fsize);
    }
    


    else if (strcmp(filetype, ".pdf") == 0 || strcmp(filetype, ".txt") == 0) {
        // Forward tar requests for .pdf or .txt files to their corresponding backend server.
        int port = (strcmp(filetype, ".pdf") == 0) ? 7100 : 7200;
        const char *tar_name = (strcmp(filetype, ".pdf") == 0) ? "pdf.tar" : "text.tar";
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            char *msg = "Failed to create socket for backend connection.\n";
            send(client_sock, msg, strlen(msg), 0);
            return;
        }
        // Set a receive timeout on the backend connection.
        struct timeval tv;
        tv.tv_sec = 10;
        tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
        struct sockaddr_in servaddr;
        servaddr.sin_family = AF_INET;
        servaddr.sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &servaddr.sin_addr);
        if (connect(sock, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
            char *msg = "Cannot connect to backend server.\n";
            send(client_sock, msg, strlen(msg), 0);
            close(sock);
            return;
        }
        // Build and send the backend command.
        char cmd2[32];
        snprintf(cmd2, sizeof(cmd2), "downltar %s", filetype);
        send(sock, cmd2, strlen(cmd2), 0);
        printf("Sent request to backend server: %s\n", cmd2);
        long fsize;
        // Receive the tar file size from the backend.
        if (recv(sock, &fsize, sizeof(long), 0) <= 0) {
            char *msg = "Failed to receive file size from backend server.\n";
            send(client_sock, msg, strlen(msg), 0);
            close(sock);
            return;
        }
        if (fsize == 0) {
            char *msg = "No files found to create tar archive.\n";
            send(client_sock, msg, strlen(msg), 0);
            close(sock);
            return;
        }
        // Relay the tar file size to the client, then forward the tar data.
        send(client_sock, &fsize, sizeof(long), 0);
        char buf[BUFSIZE];
        long recvd = 0;
        int n;
        while (recvd < fsize) {
            n = recv(sock, buf, BUFSIZE, 0);
            if (n <= 0) {
                printf("Error receiving data from backend server\n");
                break;
            }
            send(client_sock, buf, n, 0);
            recvd += n;
        }
        close(sock);
        printf("Forwarded %s to client (%ld/%ld bytes)\n", tar_name, recvd, fsize);
    }
    else {
        // Unsupported file type for tar archive.
        char *msg = "Only .c, .pdf, and .txt file types are supported for tar.\n";
        send(client_sock, msg, strlen(msg), 0);
    }
}


// collect_files_from_server: Contacts a backend server and issues a command to list file names.
// The backend's file list is received into the provided buffer.
// Returns 1 on success, or 0 if the connection fails.
int collect_files_from_server(const char *path, int port, char *buffer) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    struct sockaddr_in servaddr;
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &servaddr.sin_addr);
    if (connect(sock, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        close(sock);
        return 0;
    }

    // Build the command to list filenames.
    char cmd[BUFSIZE];
    snprintf(cmd, sizeof(cmd), "dispfnames %s", path);
    send(sock, cmd, strlen(cmd), 0);
    int n = recv(sock, buffer, BUFSIZE, 0);
    if(n < 0)
        n = 0;
    buffer[n] = '\0';
    close(sock);
    return 1;
}

// cmp_str: Helper function for qsort to sort strings alphabetically.
int cmp_str(const void *a, const void *b) {
    const char *s1 = *(const char **)a;
    const char *s2 = *(const char **)b;
    return strcmp(s1, s2);
}

// handle_dispfnames: Aggregates file names from local storage (for .c files) and from backends (for .pdf, .txt, and .zip files).
// The resulting sorted list is sent to the client.
void handle_dispfnames(int client_sock, char *cmd) {
    char dirpath[512];
    sscanf(cmd, "dispfnames %s", dirpath);

    char *home = get_home_dir();
    char local_dir[512];
    // Convert the virtual path "~S1/folder" to a local path "$HOME/S1/folder".
    snprintf(local_dir, sizeof(local_dir), "%s/%s", home, dirpath + 1);

    // Process local .c files.
    char *c_names[1024];
    int c_count = 0;
    DIR *dir = opendir(local_dir);
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_type == DT_REG) {
                const char *ext = strrchr(entry->d_name, '.');
                if (ext && strcmp(ext, ".c") == 0) {
                    c_names[c_count] = strdup(entry->d_name);
                    c_count++;
                }
            }
        }
        closedir(dir);
    }
    if (c_count > 0)
        qsort(c_names, c_count, sizeof(char*), cmp_str);
    char c_files[BUFSIZE] = "";
    for (int i = 0; i < c_count; i++) {
        strncat(c_files, c_names[i], BUFSIZE - strlen(c_files) - 1);
        strncat(c_files, "\n", BUFSIZE - strlen(c_files) - 1);
        free(c_names[i]);
    }

    // Prepare virtual backend paths for .pdf, .txt, and .zip files.
    char pdf_path[512], txt_path[512], zip_path[512];
    snprintf(pdf_path, sizeof(pdf_path), "~S2%s", dirpath + 3);
    snprintf(txt_path, sizeof(txt_path), "~S3%s", dirpath + 3);
    snprintf(zip_path, sizeof(zip_path), "~S4%s", dirpath + 3);

    char pdf_files[BUFSIZE] = "", txt_files[BUFSIZE] = "", zip_files[BUFSIZE] = "";
    char tmp[BUFSIZE] = "";

    // Retrieve lists of files from corresponding backend servers.
    if (collect_files_from_server(pdf_path, 7100, tmp)) {
        strncpy(pdf_files, tmp, BUFSIZE);
    }
    if (collect_files_from_server(txt_path, 7200, tmp)) {
        strncpy(txt_files, tmp, BUFSIZE);
    }
    if (collect_files_from_server(zip_path, 7300, tmp)) {
        strncpy(zip_files, tmp, BUFSIZE);
    }

    // Sort backend lists alphabetically. For simplicity, we tokenize by newline.
    char *pdf_arr[1024];
    int pdf_count = 0;
    char *token = strtok(pdf_files, "\n");
    while (token != NULL && pdf_count < 1024) {
        pdf_arr[pdf_count++] = token;
        token = strtok(NULL, "\n");
    }
    if (pdf_count > 0)
        qsort(pdf_arr, pdf_count, sizeof(char*), cmp_str);
    char sorted_pdf[BUFSIZE] = "";
    for (int i = 0; i < pdf_count; i++) {
        strncat(sorted_pdf, pdf_arr[i], BUFSIZE - strlen(sorted_pdf) - 1);
        strncat(sorted_pdf, "\n", BUFSIZE - strlen(sorted_pdf) - 1);
    }

    char *txt_arr[1024];
    int txt_count = 0;
    token = strtok(txt_files, "\n");
    while (token != NULL && txt_count < 1024) {
        txt_arr[txt_count++] = token;
        token = strtok(NULL, "\n");
    }
    if (txt_count > 0)
        qsort(txt_arr, txt_count, sizeof(char*), cmp_str);
    char sorted_txt[BUFSIZE] = "";
    for (int i = 0; i < txt_count; i++) {
        strncat(sorted_txt, txt_arr[i], BUFSIZE - strlen(sorted_txt) - 1);
        strncat(sorted_txt, "\n", BUFSIZE - strlen(sorted_txt) - 1);
    }

    char *zip_arr[1024];
    int zip_count = 0;
    token = strtok(zip_files, "\n");
    while (token != NULL && zip_count < 1024) {
        zip_arr[zip_count++] = token;
        token = strtok(NULL, "\n");
    }
    if (zip_count > 0)
        qsort(zip_arr, zip_count, sizeof(char*), cmp_str);
    char sorted_zip[BUFSIZE] = "";
    for (int i = 0; i < zip_count; i++) {
        strncat(sorted_zip, zip_arr[i], BUFSIZE - strlen(sorted_zip) - 1);
        strncat(sorted_zip, "\n", BUFSIZE - strlen(sorted_zip) - 1);
    }

    // Combine all file lists into one final result string.
    char final[4 * BUFSIZE] = "";
    strcat(final, c_files);
    strcat(final, sorted_pdf);
    strcat(final, sorted_txt);
    strcat(final, sorted_zip);

    // Send the final list to the client, or an error message if no files were found.
    if (strlen(final) == 0) {
        char *msg = "No files found in the specified path.\n";
        send(client_sock, msg, strlen(msg), 0);
    } else {
        send(client_sock, final, strlen(final), 0);
    }
}
