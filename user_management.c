#include "server.h"

user_t* find_user(const char* username) {
    if (!username) return NULL;
    
    pthread_mutex_lock(&user_list_mutex);
    user_t* current = user_list;
    
    while (current) {
        if (strcmp(current->username, username) == 0) {
            pthread_mutex_unlock(&user_list_mutex);
            return current;
        }
        current = current->next;
    }
    
    pthread_mutex_unlock(&user_list_mutex);
    return NULL;
}

user_t* create_user(const char* username, const char* password) {
    if (!username || !password) return NULL;
    
    user_t* user = malloc(sizeof(user_t));
    if (!user) return NULL;
    
    strncpy(user->username, username, MAX_USERNAME - 1);
    user->username[MAX_USERNAME - 1] = '\0';
    
    strncpy(user->password, password, MAX_PASSWORD - 1);
    user->password[MAX_PASSWORD - 1] = '\0';
    
    user->files = NULL;
    user->total_quota_used = 0;
    user->is_logged_in = 0;
    user->next = NULL;
    
    if (pthread_mutex_init(&user->user_mutex, NULL) != 0) {
        free(user);
        return NULL;
    }
    
    return user;
}

int add_user(user_t* user) {
    if (!user) return -1;
    
    pthread_mutex_lock(&user_list_mutex);
    
    // Check if user already exists
    user_t* existing = user_list;
    while (existing) {
        if (strcmp(existing->username, user->username) == 0) {
            pthread_mutex_unlock(&user_list_mutex);
            return -1; // User already exists
        }
        existing = existing->next;
    }
    
    // Add to head of list
    user->next = user_list;
    user_list = user;
    
    pthread_mutex_unlock(&user_list_mutex);
    
    // Create user directory
    create_user_directory(user->username);
    
    return 0;
}

int authenticate_user(const char* username, const char* password) {
    if (!username || !password) return -1;
    
    user_t* user = find_user(username);
    if (!user) return -1;
    
    pthread_mutex_lock(&user->user_mutex);
    
    if (strcmp(user->password, password) == 0) {
        user->is_logged_in = 1;
        pthread_mutex_unlock(&user->user_mutex);
        return 0; // Success
    }
    
    pthread_mutex_unlock(&user->user_mutex);
    return -1; // Invalid password
}

int logout_user(const char* username) {
    if (!username) return -1;
    
    user_t* user = find_user(username);
    if (!user) return -1;
    
    pthread_mutex_lock(&user->user_mutex);
    user->is_logged_in = 0;
    pthread_mutex_unlock(&user->user_mutex);
    
    return 0;
}

void create_user_directory(const char* username) {
    char path[512];
    snprintf(path, sizeof(path), "users/%s", username);
    
    struct stat st = {0};
    if (stat("users", &st) == -1) {
        mkdir("users", 0700);
    }
    
    if (stat(path, &st) == -1) {
        mkdir(path, 0700);
    }
}
