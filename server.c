#include "server.h"

// Global variables
thread_safe_queue_t* client_queue;
thread_safe_queue_t* task_queue;
user_t* user_list = NULL;
pthread_mutex_t user_list_mutex = PTHREAD_MUTEX_INITIALIZER;
int server_running = 1;
int next_task_id = 1;

void* client_thread_func(void* arg) {
    (void)arg; // Suppress unused parameter warning
    
    while (server_running) {
        int client_socket = (int)(intptr_t)dequeue(client_queue);
        if (client_socket < 0) continue;

        // Track currently authenticated user for this connection
        char current_username[MAX_USERNAME] = {0};

        // Process multiple commands from this client until disconnect
        while (server_running) {
            char buffer[MAX_COMMAND];
            ssize_t bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
            if (bytes_received <= 0) {
                close(client_socket);
                break;
            }

            buffer[bytes_received] = '\0';

            // Trim leading whitespace/newlines
            char* p = buffer;
            while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
                p++;
            }
            // Ignore empty lines
            if (*p == '\0') {
                continue;
            }
            // Handle quit (case-insensitive) locally
            if (strncasecmp(p, "quit", 4) == 0) {
                close(client_socket);
                break;
            }

            // Parse command and create task
            command_type_t cmd_type = parse_command(p);
            char username[MAX_USERNAME] = {0};
            char filename[MAX_FILENAME] = {0};
            char data[MAX_RESPONSE] = {0};
            size_t data_size = 0;

            // Simple parsing - in a real implementation, you'd have a proper protocol
            if (cmd_type == CMD_SIGNUP || cmd_type == CMD_LOGIN) {
                sscanf(buffer, "%*s %s %s", username, data);
                data_size = strlen(data);
                // Track the username on this connection after LOGIN attempt
                if (cmd_type == CMD_LOGIN && username[0] != '\0') {
                    strncpy(current_username, username, MAX_USERNAME - 1);
                    current_username[MAX_USERNAME - 1] = '\0';
                }
            } else if (cmd_type == CMD_UPLOAD) {
                sscanf(buffer, "%*s %s", filename);
                // In a real implementation, you'd read the file data separately
                strcpy(data, "file_data_placeholder");
                data_size = strlen(data);
                // Use current logged-in user context
                if (current_username[0] != '\0') {
                    strncpy(username, current_username, MAX_USERNAME - 1);
                    username[MAX_USERNAME - 1] = '\0';
                }
            } else if (cmd_type == CMD_DOWNLOAD || cmd_type == CMD_DELETE) {
                sscanf(buffer, "%*s %s", filename);
                if (current_username[0] != '\0') {
                    strncpy(username, current_username, MAX_USERNAME - 1);
                    username[MAX_USERNAME - 1] = '\0';
                }
            } else if (cmd_type == CMD_LOGOUT) {
                // If a username is provided, use it; otherwise use current context
                if (sscanf(buffer, "%*s %s", username) != 1) {
                    if (current_username[0] != '\0') {
                        strncpy(username, current_username, MAX_USERNAME - 1);
                        username[MAX_USERNAME - 1] = '\0';
                    }
                }
                // Clear context after logout command is issued
                current_username[0] = '\0';
            } else if (cmd_type == CMD_LIST) {
                // LIST does not include username in protocol; use current context
                if (current_username[0] != '\0') {
                    strncpy(username, current_username, MAX_USERNAME - 1);
                    username[MAX_USERNAME - 1] = '\0';
                }
            }

            // Create and enqueue task
            task_t* task = create_task(client_socket, username, cmd_type, filename, data, data_size);
            if (task) {
                enqueue(task_queue, task);
            }

            // If client sent quit, close and stop processing this socket
            if (strncmp(buffer, "quit", 4) == 0) {
                close(client_socket);
                break;
            }
        }
    }
    
    return NULL;
}

void* worker_thread_func(void* arg) {
    (void)arg; // Suppress unused parameter warning
    
    while (server_running) {
        task_t* task = (task_t*)dequeue(task_queue);
        if (task) {
            process_task(task);
        }
    }
    
    return NULL;
}

void* accept_loop(void* arg) {
    int server_socket = *(int*)arg;
    
    while (server_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
        if (client_socket < 0) {
            if (server_running) {
                perror("accept");
            }
            continue;
        }
        
        printf("New client connected from %s:%d\n", 
               inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        
        // Enqueue client socket
        enqueue(client_queue, (void*)(intptr_t)client_socket);
    }
    
    return NULL;
}

int start_server(int port) {
    // Initialize queues
    client_queue = create_queue(MAX_CLIENTS);
    task_queue = create_queue(MAX_CLIENTS * 2);
    
    if (!client_queue || !task_queue) {
        printf("Failed to create queues\n");
        if (client_queue) destroy_queue(client_queue);
        if (task_queue) destroy_queue(task_queue);
        return -1;
    }
    
    // Create server socket
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("socket");
        return -1;
    }
    
    // Set socket options
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Bind socket
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_socket);
        return -1;
    }
    
    // Listen for connections
    if (listen(server_socket, 10) < 0) {
        perror("listen");
        close(server_socket);
        return -1;
    }
    
    printf("Server listening on port %d\n", port);
    
    // Create threads
    pthread_t accept_thread;
    pthread_t client_threads[CLIENT_THREADS];
    pthread_t worker_threads[WORKER_THREADS];
    
    // Start accept thread
    if (pthread_create(&accept_thread, NULL, accept_loop, &server_socket) != 0) {
        perror("pthread_create accept_thread");
        close(server_socket);
        return -1;
    }
    
    // Start client threads
    for (int i = 0; i < CLIENT_THREADS; i++) {
        if (pthread_create(&client_threads[i], NULL, client_thread_func, NULL) != 0) {
            perror("pthread_create client_thread");
            close(server_socket);
            return -1;
        }
    }
    
    // Start worker threads
    for (int i = 0; i < WORKER_THREADS; i++) {
        if (pthread_create(&worker_threads[i], NULL, worker_thread_func, NULL) != 0) {
            perror("pthread_create worker_thread");
            close(server_socket);
            return -1;
        }
    }
    
    // Wait for threads
    pthread_join(accept_thread, NULL);
    
    for (int i = 0; i < CLIENT_THREADS; i++) {
        pthread_join(client_threads[i], NULL);
    }
    
    for (int i = 0; i < WORKER_THREADS; i++) {
        pthread_join(worker_threads[i], NULL);
    }
    
    close(server_socket);
    return 0;
}

void cleanup_server() {
    server_running = 0;
    
    // Cleanup user list
    pthread_mutex_lock(&user_list_mutex);
    user_t* current = user_list;
    while (current) {
        user_t* next = current->next;
        pthread_mutex_destroy(&current->user_mutex);
        free(current);
        current = next;
    }
    user_list = NULL;
    pthread_mutex_unlock(&user_list_mutex);
    
    // Cleanup queues
    if (client_queue) destroy_queue(client_queue);
    if (task_queue) destroy_queue(task_queue);
}

void signal_handler(int sig) {
    (void)sig; // Suppress unused parameter warning
    printf("\nShutting down server...\n");
    cleanup_server();
    exit(0);
}

int main() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("Starting multi-threaded file server...\n");
    
    int result = start_server(SERVER_PORT);
    cleanup_server();
    
    return result;
}

