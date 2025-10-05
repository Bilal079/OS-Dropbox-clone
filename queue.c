#include "server.h"

thread_safe_queue_t* create_queue(int capacity) {
    thread_safe_queue_t* queue = malloc(sizeof(thread_safe_queue_t));
    if (!queue) return NULL;
    
    queue->items = malloc(sizeof(void*) * capacity);
    if (!queue->items) {
        free(queue);
        return NULL;
    }
    
    queue->front = 0;
    queue->rear = -1;
    queue->size = 0;
    queue->capacity = capacity;
    
    if (pthread_mutex_init(&queue->mutex, NULL) != 0) {
        free(queue->items);
        free(queue);
        return NULL;
    }
    
    if (pthread_cond_init(&queue->not_empty, NULL) != 0) {
        pthread_mutex_destroy(&queue->mutex);
        free(queue->items);
        free(queue);
        return NULL;
    }
    
    if (pthread_cond_init(&queue->not_full, NULL) != 0) {
        pthread_cond_destroy(&queue->not_empty);
        pthread_mutex_destroy(&queue->mutex);
        free(queue->items);
        free(queue);
        return NULL;
    }
    
    return queue;
}

void destroy_queue(thread_safe_queue_t* queue) {
    if (!queue) return;
    
    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->not_empty);
    pthread_cond_destroy(&queue->not_full);
    
    free(queue->items);
    free(queue);
}

int enqueue(thread_safe_queue_t* queue, void* item) {
    if (!queue || !item) return -1;
    
    pthread_mutex_lock(&queue->mutex);
    
    while (queue->size >= queue->capacity) {
        pthread_cond_wait(&queue->not_full, &queue->mutex);
    }
    
    queue->rear = (queue->rear + 1) % queue->capacity;
    queue->items[queue->rear] = item;
    queue->size++;
    
    pthread_cond_signal(&queue->not_empty);
    pthread_mutex_unlock(&queue->mutex);
    
    return 0;
}

void* dequeue(thread_safe_queue_t* queue) {
    if (!queue) return NULL;
    
    pthread_mutex_lock(&queue->mutex);
    
    while (queue->size == 0) {
        pthread_cond_wait(&queue->not_empty, &queue->mutex);
    }
    
    void* item = queue->items[queue->front];
    queue->front = (queue->front + 1) % queue->capacity;
    queue->size--;
    
    pthread_cond_signal(&queue->not_full);
    pthread_mutex_unlock(&queue->mutex);
    
    return item;
}

int is_empty(thread_safe_queue_t* queue) {
    if (!queue) return 1;
    
    pthread_mutex_lock(&queue->mutex);
    int empty = (queue->size == 0);
    pthread_mutex_unlock(&queue->mutex);
    
    return empty;
}

int is_full(thread_safe_queue_t* queue) {
    if (!queue) return 1;
    
    pthread_mutex_lock(&queue->mutex);
    int full = (queue->size >= queue->capacity);
    pthread_mutex_unlock(&queue->mutex);
    
    return full;
}
