#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/time.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8080
#define BUFFER_SIZE 1024

int connect_to_server() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return -1;
    }
    
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);
    
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(sock);
        return -1;
    }
    
    return sock;
}

void send_command(int sock, const char* command) {
    send(sock, command, strlen(command), 0);
}

void receive_response(int sock) {
    char buffer[BUFFER_SIZE];
    struct timeval tv;
    fd_set readfds;
    // Read whatever is readily available, then stop after a short idle
    while (1) {
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        tv.tv_sec = 0;
        tv.tv_usec = 200000; // 200ms
        int ready = select(sock + 1, &readfds, NULL, NULL, &tv);
        if (ready <= 0) {
            break; // timeout or error, stop collecting
        }
        ssize_t bytes_received = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0) {
            break;
        }
        buffer[bytes_received] = '\0';
        printf("Server: %s", buffer);
        // If response ends with newline, continue to allow multiple lines
    }
}

int main() {
    int sock = connect_to_server();
    if (sock < 0) {
        printf("Failed to connect to server\n");
        return 1;
    }
    
    printf("Connected to server. Type 'quit' to exit.\n");
    printf("Available commands:\n");
    printf("  SIGNUP <username> <password>\n");
    printf("  LOGIN <username> <password>\n");
    printf("  LOGOUT <username>\n");
    printf("  UPLOAD <filename>\n");
    printf("  DOWNLOAD <filename>\n");
    printf("  DELETE <filename>\n");
    printf("  LIST\n");
    printf("\n");
    
    char input[BUFFER_SIZE];
    
    while (1) {
        printf("> ");
        if (!fgets(input, sizeof(input), stdin)) {
            break;
        }
        
        // Remove newline
        input[strcspn(input, "\n")] = 0;
        
        if (strcmp(input, "quit") == 0) {
            break;
        }
        
        send_command(sock, input);
        send_command(sock, "\n");
        receive_response(sock);
    }
    
    close(sock);
    printf("Disconnected from server\n");
    return 0;
}

