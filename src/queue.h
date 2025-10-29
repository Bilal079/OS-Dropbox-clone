#ifndef QUEUE_H
#define QUEUE_H

#include <pthread.h>
#include <stddef.h>

typedef struct {
    void **buffer;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
    int closed;
} ts_queue_t;

int ts_queue_init(ts_queue_t *q, size_t capacity);
void ts_queue_close(ts_queue_t *q); // wake all blocked ops
void ts_queue_destroy(ts_queue_t *q);
// returns 0 on success, -1 if closed
int ts_queue_push(ts_queue_t *q, void *item);
// returns 0 on success, -1 if closed and empty
int ts_queue_pop(ts_queue_t *q, void **out_item);

#endif


