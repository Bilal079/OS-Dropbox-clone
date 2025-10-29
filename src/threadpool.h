#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include "queue.h"
#include "lockmgr.h"

typedef enum { TASK_UPLOAD, TASK_DOWNLOAD, TASK_DELETE, TASK_LIST } task_type_t;

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t done_cv;
    int done;
    int status; // 0 ok, -1 err
    char *err_msg;
    // response payloads
    // For LIST: names combined with \n, for DOWNLOAD: temp file path to stream back
    char *resp_path;
    char **list_names;
    int list_count;
} task_result_t;

typedef struct {
    task_type_t type;
    int client_fd;
    long long user_id;
    char *username;
    char *filename;
    long long size;
    char *upload_tmp_path; // path to temp uploaded content (already received by client thread)
    task_result_t result;
} task_t;

void task_init(task_t *t);
void task_free(task_t *t);

typedef struct {
    ts_queue_t *task_queue;
    int worker_count;
    pthread_t *workers;
    const char *root_dir;
    void *db; // db_t* opaque to avoid header dep
    lockmgr_t *locks;
} worker_pool_t;

int worker_pool_start(worker_pool_t *wp, ts_queue_t *task_queue, int worker_count, const char *root_dir, void *db_ptr, lockmgr_t *locks);
void worker_pool_stop(worker_pool_t *wp);

#endif


