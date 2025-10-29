#include "threadpool.h"
#include "db.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>

static void task_result_init(task_result_t *r) {
    pthread_mutex_init(&r->mutex, NULL);
    pthread_cond_init(&r->done_cv, NULL);
    r->done = 0;
    r->status = 0;
    r->err_msg = NULL;
    r->resp_path = NULL;
    r->list_names = NULL;
    r->list_count = 0;
}

static void task_result_destroy(task_result_t *r) {
    pthread_mutex_destroy(&r->mutex);
    pthread_cond_destroy(&r->done_cv);
    free(r->err_msg);
    free(r->resp_path);
    if (r->list_names) {
        for (int i = 0; i < r->list_count; i++) free(r->list_names[i]);
        free(r->list_names);
    }
}

void task_init(task_t *t) {
    memset(t, 0, sizeof(*t));
    task_result_init(&t->result);
}

void task_free(task_t *t) {
    free(t->username);
    free(t->filename);
    free(t->upload_tmp_path);
    task_result_destroy(&t->result);
}

static int ensure_user_dir(const char *root, const char *username, char *out_path, size_t out_sz) {
    int n = snprintf(out_path, out_sz, "%s/%s", root, username);
    if (n <= 0 || (size_t)n >= out_sz) return -1;
    mkdir(out_path, 0755);
    return 0;
}

static int join_user_file(const char *root, const char *username, const char *name, char *out_path, size_t out_sz) {
    if (ensure_user_dir(root, username, out_path, out_sz) != 0) return -1;
    size_t len = strlen(out_path);
    int n = snprintf(out_path + len, out_sz - len, "/%s", name);
    if (n <= 0 || (size_t)n >= out_sz - len) return -1;
    return 0;
}

static int move_file(const char *src, const char *dst) {
    if (rename(src, dst) == 0) return 0;
    // fallback copy
    int in = open(src, O_RDONLY);
    if (in < 0) return -1;
    int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0) { close(in); return -1; }
    char buf[64 * 1024];
    ssize_t r;
    while ((r = read(in, buf, sizeof(buf))) > 0) {
        if (write_n(out, buf, (size_t)r) < 0) { close(in); close(out); return -1; }
    }
    close(in); close(out);
    unlink(src);
    return 0;
}

static void set_error(task_result_t *res, const char *msg) {
    res->status = -1;
    free(res->err_msg);
    res->err_msg = strdup(msg);
}

static void worker_handle_upload(worker_pool_t *wp, task_t *t, db_t *db) {
    // Serialize conflicting ops: user write and file write
    lockmgr_user_lock(wp->locks, t->username, 1);
    lockmgr_file_lock(wp->locks, t->username, t->filename, 1);
    char final_path[1024];
    if (join_user_file(wp->root_dir, t->username, t->filename, final_path, sizeof(final_path)) != 0) {
        set_error(&t->result, "PATH");
        goto out;
    }
    // Move temp file into place
    if (move_file(t->upload_tmp_path, final_path) != 0) {
        set_error(&t->result, "MOVE");
        goto out;
    }
    long long delta = 0;
    if (db_upsert_file(db, t->user_id, t->filename, t->size, &delta) != 0) {
        set_error(&t->result, "DB");
        goto out;
    }
out:
    lockmgr_file_unlock(wp->locks, t->username, t->filename, 1);
    lockmgr_user_unlock(wp->locks, t->username, 1);
}

static void worker_handle_download(worker_pool_t *wp, task_t *t, db_t *db) {
    (void)db;
    lockmgr_file_lock(wp->locks, t->username, t->filename, 0);
    char final_path[1024];
    if (join_user_file(wp->root_dir, t->username, t->filename, final_path, sizeof(final_path)) != 0) {
        set_error(&t->result, "PATH");
        goto out;
    }
    struct stat st;
    if (stat(final_path, &st) != 0) {
        set_error(&t->result, "NOFILE");
        goto out;
    }
    t->size = st.st_size;
    t->result.resp_path = strdup(final_path);
out:
    lockmgr_file_unlock(wp->locks, t->username, t->filename, 0);
}

static void worker_handle_delete(worker_pool_t *wp, task_t *t, db_t *db) {
    lockmgr_user_lock(wp->locks, t->username, 1);
    lockmgr_file_lock(wp->locks, t->username, t->filename, 1);
    char final_path[1024];
    if (join_user_file(wp->root_dir, t->username, t->filename, final_path, sizeof(final_path)) != 0) {
        set_error(&t->result, "PATH");
        goto out;
    }
    struct stat st;
    long long sz = 0;
    if (stat(final_path, &st) == 0) sz = st.st_size;
    unlink(final_path);
    long long del_sz = 0;
    if (db_delete_file(db, t->user_id, t->filename, &del_sz) != 0) {
        // if DB delete fails, try to restore? For MVP, report error.
        set_error(&t->result, "DB");
        goto out;
    }
    (void)sz;
out:
    lockmgr_file_unlock(wp->locks, t->username, t->filename, 1);
    lockmgr_user_unlock(wp->locks, t->username, 1);
}

static void worker_handle_list(worker_pool_t *wp, task_t *t, db_t *db) {
    (void)wp;
    // Allow concurrent readers; serialize against writers via user read lock
    lockmgr_user_lock(wp->locks, t->username ? t->username : "", 0);
    if (db_list_files(db, t->user_id, &t->result.list_names, &t->result.list_count) != 0) {
        set_error(&t->result, "DB");
        lockmgr_user_unlock(wp->locks, t->username ? t->username : "", 0);
        return;
    }
    lockmgr_user_unlock(wp->locks, t->username ? t->username : "", 0);
}

static void *worker_main(void *arg) {
    worker_pool_t *wp = (worker_pool_t*)arg;
    db_t *db = (db_t*)wp->db;
    for (;;) {
        void *item = NULL;
        if (ts_queue_pop(wp->task_queue, &item) != 0) break;
        task_t *t = (task_t*)item;
        switch (t->type) {
            case TASK_UPLOAD: worker_handle_upload(wp, t, db); break;
            case TASK_DOWNLOAD: worker_handle_download(wp, t, db); break;
            case TASK_DELETE: worker_handle_delete(wp, t, db); break;
            case TASK_LIST: worker_handle_list(wp, t, db); break;
        }
        pthread_mutex_lock(&t->result.mutex);
        t->result.done = 1;
        pthread_cond_signal(&t->result.done_cv);
        pthread_mutex_unlock(&t->result.mutex);
    }
    return NULL;
}

int worker_pool_start(worker_pool_t *wp, ts_queue_t *task_queue, int worker_count, const char *root_dir, void *db_ptr, lockmgr_t *locks) {
    wp->task_queue = task_queue;
    wp->worker_count = worker_count;
    wp->workers = (pthread_t*)calloc((size_t)worker_count, sizeof(pthread_t));
    wp->root_dir = root_dir;
    wp->db = db_ptr;
    wp->locks = locks;
    for (int i = 0; i < worker_count; i++) {
        pthread_create(&wp->workers[i], NULL, worker_main, wp);
    }
    return 0;
}

void worker_pool_stop(worker_pool_t *wp) {
    ts_queue_close(wp->task_queue);
    for (int i = 0; i < wp->worker_count; i++) {
        pthread_join(wp->workers[i], NULL);
    }
    free(wp->workers);
    wp->workers = NULL;
}


