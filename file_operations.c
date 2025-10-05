#include "server.h"

int upload_file(user_t* user, const char* filename, const char* data, size_t data_size) {
    if (!user || !filename || !data) return -1;
    
    pthread_mutex_lock(&user->user_mutex);
    
    // Check if user is logged in
    if (!user->is_logged_in) {
        pthread_mutex_unlock(&user->user_mutex);
        return -1;
    }
    
    // Check quota
    if (!check_quota(user, data_size)) {
        pthread_mutex_unlock(&user->user_mutex);
        return -1;
    }
    
    // Check if file already exists and remove it first
    file_metadata_t* existing = user->files;
    file_metadata_t* prev = NULL;
    while (existing) {
        if (strcmp(existing->filename, filename) == 0) {
            // Remove from quota
            user->total_quota_used -= existing->size;
            // Remove from list
            if (prev) {
                prev->next = existing->next;
            } else {
                user->files = existing->next;
            }
            free(existing);
            break;
        }
        prev = existing;
        existing = existing->next;
    }
    
    // Create file path
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "users/%s/%s", user->username, filename);
    
    // Write file
    FILE* file = fopen(filepath, "wb");
    if (!file) {
        pthread_mutex_unlock(&user->user_mutex);
        return -1;
    }
    
    size_t written = fwrite(data, 1, data_size, file);
    fclose(file);
    
    if (written != data_size) {
        pthread_mutex_unlock(&user->user_mutex);
        return -1;
    }
    
    // Add to metadata
    file_metadata_t* new_file = malloc(sizeof(file_metadata_t));
    if (!new_file) {
        pthread_mutex_unlock(&user->user_mutex);
        return -1;
    }
    
    strncpy(new_file->filename, filename, MAX_FILENAME - 1);
    new_file->filename[MAX_FILENAME - 1] = '\0';
    new_file->size = data_size;
    new_file->created_time = time(NULL);
    new_file->next = user->files;
    user->files = new_file;
    
    // Update quota
    user->total_quota_used += data_size;
    
    pthread_mutex_unlock(&user->user_mutex);
    return 0;
}

int download_file(user_t* user, const char* filename, char* buffer, size_t* buffer_size) {
    if (!user || !filename || !buffer || !buffer_size) return -1;
    
    pthread_mutex_lock(&user->user_mutex);
    
    if (!user->is_logged_in) {
        pthread_mutex_unlock(&user->user_mutex);
        return -1;
    }
    
    // Find file in metadata
    file_metadata_t* file = user->files;
    while (file) {
        if (strcmp(file->filename, filename) == 0) {
            break;
        }
        file = file->next;
    }
    
    if (!file) {
        pthread_mutex_unlock(&user->user_mutex);
        return -1;
    }
    
    // Read file
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "users/%s/%s", user->username, filename);
    
    FILE* fp = fopen(filepath, "rb");
    if (!fp) {
        pthread_mutex_unlock(&user->user_mutex);
        return -1;
    }
    
    size_t read_size = fread(buffer, 1, *buffer_size, fp);
    fclose(fp);
    
    *buffer_size = read_size;
    pthread_mutex_unlock(&user->user_mutex);
    
    return 0;
}

int delete_file(user_t* user, const char* filename) {
    if (!user || !filename) return -1;
    
    pthread_mutex_lock(&user->user_mutex);
    
    if (!user->is_logged_in) {
        pthread_mutex_unlock(&user->user_mutex);
        return -1;
    }
    
    // Find file in metadata
    file_metadata_t* file = user->files;
    file_metadata_t* prev = NULL;
    
    while (file) {
        if (strcmp(file->filename, filename) == 0) {
            // Remove from quota
            user->total_quota_used -= file->size;
            
            // Remove from list
            if (prev) {
                prev->next = file->next;
            } else {
                user->files = file->next;
            }
            
            // Delete physical file
            char filepath[512];
            snprintf(filepath, sizeof(filepath), "users/%s/%s", user->username, filename);
            unlink(filepath);
            
            free(file);
            pthread_mutex_unlock(&user->user_mutex);
            return 0;
        }
        prev = file;
        file = file->next;
    }
    
    pthread_mutex_unlock(&user->user_mutex);
    return -1;
}

int list_files(user_t* user, char* response, size_t response_size) {
    if (!user || !response) return -1;
    
    pthread_mutex_lock(&user->user_mutex);
    
    if (!user->is_logged_in) {
        pthread_mutex_unlock(&user->user_mutex);
        return -1;
    }
    
    int offset = 0;
    offset += snprintf(response + offset, response_size - offset, 
                      "Files for user %s:\n", user->username);
    offset += snprintf(response + offset, response_size - offset,
                      "Total quota used: %zu bytes\n", user->total_quota_used);
    offset += snprintf(response + offset, response_size - offset,
                      "Files:\n");
    
    file_metadata_t* file = user->files;
    while (file && offset < response_size - 100) {
        char timestamp[64];
        get_timestamp(timestamp, sizeof(timestamp));
        
        offset += snprintf(response + offset, response_size - offset,
                          "  %s (%zu bytes) - %s\n", 
                          file->filename, file->size, timestamp);
        file = file->next;
    }
    
    pthread_mutex_unlock(&user->user_mutex);
    return 0;
}

int check_quota(user_t* user, size_t additional_size) {
    if (!user) return 0;
    
    return (user->total_quota_used + additional_size) <= MAX_TOTAL_QUOTA;
}

int file_exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0;
}

size_t get_file_size(const char* path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return st.st_size;
    }
    return 0;
}

void get_timestamp(char* buffer, size_t size) {
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", tm_info);
}
