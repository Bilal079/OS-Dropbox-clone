#include "server.h"

command_type_t parse_command(const char* command) {
    if (!command) return CMD_UNKNOWN;

    // Skip leading whitespace and CR/LF
    const char* p = command;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }
    
    if (strncmp(p, "UPLOAD", 6) == 0) return CMD_UPLOAD;
    if (strncmp(p, "DOWNLOAD", 8) == 0) return CMD_DOWNLOAD;
    if (strncmp(p, "DELETE", 6) == 0) return CMD_DELETE;
    if (strncmp(p, "LIST", 4) == 0) return CMD_LIST;
    if (strncmp(p, "SIGNUP", 6) == 0) return CMD_SIGNUP;
    if (strncmp(p, "LOGIN", 5) == 0) return CMD_LOGIN;
    if (strncmp(p, "LOGOUT", 6) == 0) return CMD_LOGOUT;
    
    return CMD_UNKNOWN;
}

task_t* create_task(int client_socket, const char* username, command_type_t cmd_type, 
                   const char* filename, const char* data, size_t data_size) {
    task_t* task = malloc(sizeof(task_t));
    if (!task) return NULL;
    
    task->client_socket = client_socket;
    strncpy(task->username, username ? username : "", MAX_USERNAME - 1);
    task->username[MAX_USERNAME - 1] = '\0';
    task->cmd_type = cmd_type;
    strncpy(task->filename, filename ? filename : "", MAX_FILENAME - 1);
    task->filename[MAX_FILENAME - 1] = '\0';
    task->data_size = data_size;
    if (data && data_size > 0) {
        size_t to_copy = data_size < (MAX_RESPONSE - 1) ? data_size : (MAX_RESPONSE - 1);
        memcpy(task->data, data, to_copy);
        task->data[to_copy] = '\0';
    } else {
        task->data[0] = '\0';
    }
    task->task_id = __sync_fetch_and_add(&next_task_id, 1);
    
    return task;
}

void process_task(task_t* task) {
    if (!task) return;
    
    char response[MAX_RESPONSE] = {0};
    user_t* user = find_user(task->username);
    
    switch (task->cmd_type) {
        case CMD_SIGNUP: {
            const char* username = task->username;
            const char* password = task->data;
            if (username && username[0] != '\0' && password && password[0] != '\0') {
                user_t* new_user = create_user(username, password);
                if (new_user && add_user(new_user) == 0) {
                    snprintf(response, sizeof(response), "SUCCESS: User %s created\n", username);
                } else {
                    snprintf(response, sizeof(response), "ERROR: Failed to create user %s\n", username);
                    if (new_user) free(new_user);
                }
            } else {
                snprintf(response, sizeof(response), "ERROR: Invalid signup format\n");
            }
            break;
        }
        
        case CMD_LOGIN: {
            const char* username = task->username;
            const char* password = task->data;
            if (username && username[0] != '\0' && password && password[0] != '\0') {
                if (authenticate_user(username, password) == 0) {
                    snprintf(response, sizeof(response), "SUCCESS: User %s logged in\n", username);
                } else {
                    snprintf(response, sizeof(response), "ERROR: Invalid credentials\n");
                }
            } else {
                snprintf(response, sizeof(response), "ERROR: Invalid login format\n");
            }
            break;
        }
        
        case CMD_LOGOUT: {
            if (user) {
                logout_user(task->username);
                snprintf(response, sizeof(response), "SUCCESS: User %s logged out\n", task->username);
            } else {
                snprintf(response, sizeof(response), "ERROR: User not found\n");
            }
            break;
        }
        
        case CMD_UPLOAD: {
            if (!user) {
                snprintf(response, sizeof(response), "ERROR: User not found\n");
                break;
            }
            
            if (upload_file(user, task->filename, task->data, task->data_size) == 0) {
                snprintf(response, sizeof(response), "SUCCESS: File %s uploaded\n", task->filename);
            } else {
                snprintf(response, sizeof(response), "ERROR: Failed to upload file %s\n", task->filename);
            }
            break;
        }
        
        case CMD_DOWNLOAD: {
            if (!user) {
                snprintf(response, sizeof(response), "ERROR: User not found\n");
                break;
            }
            
            char buffer[MAX_RESPONSE];
            size_t buffer_size = sizeof(buffer);
            
            if (download_file(user, task->filename, buffer, &buffer_size) == 0) {
                snprintf(response, sizeof(response), "SUCCESS: File %s downloaded (%zu bytes)\n", 
                        task->filename, buffer_size);
                // Note: In a real implementation, you'd send the file data separately
            } else {
                snprintf(response, sizeof(response), "ERROR: Failed to download file %s\n", task->filename);
            }
            break;
        }
        
        case CMD_DELETE: {
            if (!user) {
                snprintf(response, sizeof(response), "ERROR: User not found\n");
                break;
            }
            
            if (delete_file(user, task->filename) == 0) {
                snprintf(response, sizeof(response), "SUCCESS: File %s deleted\n", task->filename);
            } else {
                snprintf(response, sizeof(response), "ERROR: Failed to delete file %s\n", task->filename);
            }
            break;
        }
        
        case CMD_LIST: {
            if (!user) {
                snprintf(response, sizeof(response), "ERROR: User not found\n");
                break;
            }
            
            if (list_files(user, response, sizeof(response)) == 0) {
                // Response already filled by list_files
            } else {
                snprintf(response, sizeof(response), "ERROR: Failed to list files\n");
            }
            break;
        }
        
        default:
            snprintf(response, sizeof(response), "ERROR: Unknown command\n");
            break;
    }
    
    send_response(task->client_socket, response);
    free(task);
}

void send_response(int client_socket, const char* response) {
    if (client_socket < 0 || !response) return;
    
    size_t len = strlen(response);
    size_t sent = 0;
    
    while (sent < len) {
        ssize_t result = send(client_socket, response + sent, len - sent, 0);
        if (result <= 0) {
            break;
        }
        sent += result;
    }
}

