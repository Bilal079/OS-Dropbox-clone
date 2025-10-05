#ifndef SERVER_H
#define SERVER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <stdint.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <dirent.h>

// Constants
#define MAX_CLIENTS 100
#define MAX_FILENAME 256
#define MAX_USERNAME 64
#define MAX_PASSWORD 64
#define MAX_COMMAND 1024
#define MAX_RESPONSE 2048
#define MAX_FILES_PER_USER 100
#define MAX_FILE_SIZE (10 * 1024 * 1024) // 10MB per file
#define MAX_TOTAL_QUOTA (100 * 1024 * 1024) // 100MB total per user
#define SERVER_PORT 8080
#define CLIENT_THREADS 5
#define WORKER_THREADS 3

// Command types
typedef enum {
    CMD_UPLOAD,
    CMD_DOWNLOAD,
    CMD_DELETE,
    CMD_LIST,
    CMD_SIGNUP,
    CMD_LOGIN,
    CMD_LOGOUT,
    CMD_UNKNOWN
} command_type_t;

// Task structure for worker queue
typedef struct {
    int client_socket;
    char username[MAX_USERNAME];
    command_type_t cmd_type;
    char filename[MAX_FILENAME];
    char data[MAX_RESPONSE];
    size_t data_size;
    int task_id;
} task_t;

// File metadata structure
typedef struct file_metadata {
    char filename[MAX_FILENAME];
    size_t size;
    time_t created_time;
    struct file_metadata* next;
} file_metadata_t;

// User structure
typedef struct user {
    char username[MAX_USERNAME];
    char password[MAX_PASSWORD];
    file_metadata_t* files;
    size_t total_quota_used;
    int is_logged_in;
    pthread_mutex_t user_mutex;
    struct user* next;
} user_t;

// Thread-safe queue structure
typedef struct {
    void** items;
    int front;
    int rear;
    int size;
    int capacity;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} thread_safe_queue_t;

// Global variables
extern thread_safe_queue_t* client_queue;
extern thread_safe_queue_t* task_queue;
extern user_t* user_list;
extern pthread_mutex_t user_list_mutex;
extern int server_running;
extern int next_task_id;

// Function prototypes

// Queue operations
thread_safe_queue_t* create_queue(int capacity);
void destroy_queue(thread_safe_queue_t* queue);
int enqueue(thread_safe_queue_t* queue, void* item);
void* dequeue(thread_safe_queue_t* queue);
int is_empty(thread_safe_queue_t* queue);
int is_full(thread_safe_queue_t* queue);

// User management
user_t* find_user(const char* username);
user_t* create_user(const char* username, const char* password);
int add_user(user_t* user);
int authenticate_user(const char* username, const char* password);
int logout_user(const char* username);

// File operations
int upload_file(user_t* user, const char* filename, const char* data, size_t data_size);
int download_file(user_t* user, const char* filename, char* buffer, size_t* buffer_size);
int delete_file(user_t* user, const char* filename);
int list_files(user_t* user, char* response, size_t response_size);
int check_quota(user_t* user, size_t additional_size);

// Command processing
command_type_t parse_command(const char* command);
task_t* create_task(int client_socket, const char* username, command_type_t cmd_type, 
                   const char* filename, const char* data, size_t data_size);
void process_task(task_t* task);
void send_response(int client_socket, const char* response);

// Thread functions
void* client_thread_func(void* arg);
void* worker_thread_func(void* arg);
void* accept_loop(void* arg);

// Server functions
int start_server(int port);
void cleanup_server();
void signal_handler(int sig);

// Utility functions
void create_user_directory(const char* username);
int file_exists(const char* path);
size_t get_file_size(const char* path);
void get_timestamp(char* buffer, size_t size);

#endif // SERVER_H
