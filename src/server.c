#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "queue.h"
#include "threadpool.h"
#include "util.h"
#include "db.h"
#include "lockmgr.h"

typedef struct {
    int client_fd;
    long long user_id;
    char username[128];
    int authenticated;
} session_t;

static volatile int g_running = 1;
static void handle_sigint(int sig) { (void)sig; g_running = 0; }

typedef struct {
    ts_queue_t client_queue;
    ts_queue_t task_queue;
    pthread_t *client_threads;
    int client_thread_count;
    worker_pool_t worker_pool;
    db_t db;
    char root_dir[512];
    lockmgr_t *locks;
    // track active client sockets for shutdown
    pthread_mutex_t clients_mu;
    int *client_fds;
    int client_fds_cap;
    int client_fds_count;
} server_state_t;

static int create_listener(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int on = 1; setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    struct sockaddr_in addr; memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET; addr.sin_addr.s_addr = htonl(INADDR_ANY); addr.sin_port = htons((uint16_t)port);
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); exit(1);} 
    if (listen(fd, 128) < 0) { perror("listen"); exit(1);} 
    return fd;
}

static char *hash_password(const char *pw) {
    // For MVP: NOT secure; replace with argon2/bcrypt later
    size_t n = strlen(pw);
    char *out = (char*)malloc(n * 2 + 1);
    for (size_t i = 0; i < n; i++) { out[i*2] = pw[i]; out[i*2+1] = 'x'; }
    out[n*2] = '\0';
    return out;
}

static int read_command_line(int fd, char *cmd, size_t sz) {
    int r = read_line(fd, cmd, sz);
    return r;
}

static int ensure_user_dir_base(const char *root, const char *username, char *out_path, size_t out_sz) {
    int n = snprintf(out_path, out_sz, "%s/%s", root, username);
    if (n <= 0 || (size_t)n >= out_sz) return -1;
    mkdir(out_path, 0755);
    return 0;
}

static int recv_upload_payload(int fd, const char *root, const char *username, long long size, char *tmp_path_out, size_t tmp_sz) {
    char basedir[1024];
    if (ensure_user_dir_base(root, username, basedir, sizeof(basedir)) != 0) return -1;
    char tmpl[1024];
    int n = snprintf(tmpl, sizeof(tmpl), "%s/.tmp.upload.XXXXXX", basedir);
    if (n <= 0 || (size_t)n >= sizeof(tmpl)) return -1;
    int tfd = mkstemp(tmpl);
    if (tfd < 0) return -1;
    int n_copied = snprintf(tmp_path_out, tmp_sz, "%s", tmpl);
    if (n_copied < 0 || (size_t)n_copied >= tmp_sz) return -1;
    char buf[64 * 1024];
    long long remain = size;
    while (remain > 0) {
        long long chunk_ll = (remain > (long long)sizeof(buf)) ? (long long)sizeof(buf) : remain;
        size_t chunk = (size_t)chunk_ll;
        int rr = read_n(fd, buf, chunk);
        if (rr <= 0) { close(tfd); return -1; }
        if (write_n(tfd, buf, chunk) < 0) { close(tfd); return -1; }
        remain -= (long long)chunk;
    }
    fsync(tfd);
    close(tfd);
    return 0;
}

static void respond_ok(int fd) { send_fmt(fd, "OK\n"); }
static void respond_err(int fd, const char *code) { send_fmt(fd, "ERR %s\n", code); }

static void handle_client(server_state_t *st, int client_fd) {
    session_t sess; memset(&sess, 0, sizeof(sess)); sess.client_fd = client_fd;
    char line[1024];
    for (;;) {
        int rr = read_command_line(client_fd, line, sizeof(line));
        if (rr <= 0) break;
        char cmd[32];
        if (sscanf(line, "%31s", cmd) != 1) { respond_err(client_fd, "PROTO"); continue; }
        if (strcmp(cmd, "SIGNUP") == 0) {
            char user[128], pass[128]; long long quota = 104857600LL; /* 100MB default */
            if (sscanf(line, "SIGNUP %127s %127s", user, pass) != 2) { respond_err(client_fd, "PROTO"); continue; }
            char *ph = hash_password(pass);
            if (db_signup(&st->db, user, ph, quota) != 0) { free(ph); respond_err(client_fd, "EXISTS"); continue; }
            free(ph);
            respond_ok(client_fd);
        } else if (strcmp(cmd, "LOGIN") == 0) {
            char user[128], pass[128];
            if (sscanf(line, "LOGIN %127s %127s", user, pass) != 2) { respond_err(client_fd, "PROTO"); continue; }
            long long uid = 0, quota=0, used=0; char *stored = NULL;
            if (db_get_user(&st->db, user, &uid, &stored, &quota, &used) != 0) { respond_err(client_fd, "AUTH"); continue; }
            char *ph = hash_password(pass);
            int ok = (stored && strcmp(stored, ph) == 0);
            free(stored); free(ph);
            if (!ok) { respond_err(client_fd, "AUTH"); continue; }
            sess.user_id = uid; snprintf(sess.username, sizeof(sess.username), "%s", user); sess.authenticated = 1;
            respond_ok(client_fd);
        } else {
            if (!sess.authenticated) { respond_err(client_fd, "AUTH"); continue; }
            if (strcmp(cmd, "UPLOAD") == 0) {
                char fname[256]; long long size = 0;
                if (sscanf(line, "UPLOAD %255s %lld", fname, &size) != 2 || size < 0) { respond_err(client_fd, "PROTO"); continue; }
                char tmp_path[256];
                if (recv_upload_payload(client_fd, st->root_dir, sess.username, size, tmp_path, sizeof(tmp_path)) != 0) { respond_err(client_fd, "IO"); continue; }
                task_t *t = (task_t*)calloc(1, sizeof(task_t));
                task_init(t);
                t->type = TASK_UPLOAD;
                t->client_fd = client_fd;
                t->user_id = sess.user_id;
                t->username = strdup(sess.username);
                t->filename = strdup(fname);
                t->size = size;
                t->upload_tmp_path = strdup(tmp_path);
                ts_queue_push(&st->task_queue, t);
                pthread_mutex_lock(&t->result.mutex);
                while (!t->result.done) pthread_cond_wait(&t->result.done_cv, &t->result.mutex);
                pthread_mutex_unlock(&t->result.mutex);
                if (t->result.status == 0) respond_ok(client_fd); else respond_err(client_fd, t->result.err_msg ? t->result.err_msg : "ERR");
                task_free(t); free(t);
            } else if (strcmp(cmd, "DOWNLOAD") == 0) {
                char fname[256];
                if (sscanf(line, "DOWNLOAD %255s", fname) != 1) { respond_err(client_fd, "PROTO"); continue; }
                task_t *t = (task_t*)calloc(1, sizeof(task_t));
                task_init(t);
                t->type = TASK_DOWNLOAD; t->client_fd = client_fd; t->user_id = sess.user_id; t->username = strdup(sess.username); t->filename = strdup(fname);
                ts_queue_push(&st->task_queue, t);
                pthread_mutex_lock(&t->result.mutex);
                while (!t->result.done) pthread_cond_wait(&t->result.done_cv, &t->result.mutex);
                pthread_mutex_unlock(&t->result.mutex);
                if (t->result.status != 0 || !t->result.resp_path) { respond_err(client_fd, t->result.err_msg ? t->result.err_msg : "ERR"); task_free(t); free(t); continue; }
                // stream file
                int fd = open(t->result.resp_path, O_RDONLY);
                if (fd < 0) { respond_err(client_fd, "IO"); task_free(t); free(t); continue; }
                struct stat st;
                fstat(fd, &st);
                send_fmt(client_fd, "OK %lld\n", (long long)st.st_size);
                char buf[64 * 1024]; ssize_t r;
                while ((r = read(fd, buf, sizeof(buf))) > 0) {
                    if (write_n(client_fd, buf, (size_t)r) < 0) { break; }
                }
                close(fd);
                task_free(t); free(t);
            } else if (strcmp(cmd, "DELETE") == 0) {
                char fname[256];
                if (sscanf(line, "DELETE %255s", fname) != 1) { respond_err(client_fd, "PROTO"); continue; }
                task_t *t = (task_t*)calloc(1, sizeof(task_t));
                task_init(t);
                t->type = TASK_DELETE; t->client_fd = client_fd; t->user_id = sess.user_id; t->username = strdup(sess.username); t->filename = strdup(fname);
                ts_queue_push(&st->task_queue, t);
                pthread_mutex_lock(&t->result.mutex);
                while (!t->result.done) pthread_cond_wait(&t->result.done_cv, &t->result.mutex);
                pthread_mutex_unlock(&t->result.mutex);
                if (t->result.status == 0) respond_ok(client_fd); else respond_err(client_fd, t->result.err_msg ? t->result.err_msg : "ERR");
                task_free(t); free(t);
            } else if (strcmp(cmd, "LIST") == 0) {
                task_t *t = (task_t*)calloc(1, sizeof(task_t));
                task_init(t);
                t->type = TASK_LIST; t->client_fd = client_fd; t->user_id = sess.user_id; t->username = strdup(sess.username);
                ts_queue_push(&st->task_queue, t);
                pthread_mutex_lock(&t->result.mutex);
                while (!t->result.done) pthread_cond_wait(&t->result.done_cv, &t->result.mutex);
                pthread_mutex_unlock(&t->result.mutex);
                if (t->result.status != 0) { respond_err(client_fd, t->result.err_msg ? t->result.err_msg : "ERR"); task_free(t); free(t); continue; }
                send_fmt(client_fd, "OK %d\n", t->result.list_count);
                for (int i = 0; i < t->result.list_count; i++) {
                    send_fmt(client_fd, "%s\n", t->result.list_names[i]);
                }
                task_free(t); free(t);
            } else {
                respond_err(client_fd, "UNKNOWN");
            }
        }
    }
    close(client_fd);
}

static void *client_thread_main(void *arg) {
    server_state_t *st = (server_state_t*)arg;
    for (;;) {
        void *item = NULL;
        if (ts_queue_pop(&st->client_queue, &item) != 0) break;
        int client_fd = (int)(intptr_t)item;
        handle_client(st, client_fd);
    }
    return NULL;
}

int main(int argc, char **argv) {
    int port = 9000; const char *root = "storage"; const char *dbpath = "storage/meta.db";
    long long default_quota = 104857600LL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i+1 < argc) port = atoi(argv[++i]);
        else if (strcmp(argv[i], "--root") == 0 && i+1 < argc) root = argv[++i];
        else if (strcmp(argv[i], "--db") == 0 && i+1 < argc) dbpath = argv[++i];
        else if (strcmp(argv[i], "--quota-bytes") == 0 && i+1 < argc) default_quota = atoll(argv[++i]);
    }
    signal(SIGINT, handle_sigint);
    mkdir(root, 0755);

    server_state_t st; memset(&st, 0, sizeof(st));
    snprintf(st.root_dir, sizeof(st.root_dir), "%s", root);
    if (ts_queue_init(&st.client_queue, 128) != 0) return 1;
    if (ts_queue_init(&st.task_queue, 1024) != 0) return 1;
    if (db_open(&st.db, dbpath) != 0) { fprintf(stderr, "DB open failed\n"); return 1; }
    if (lockmgr_init(&st.locks) != 0) { fprintf(stderr, "Lockmgr init failed\n"); return 1; }
    pthread_mutex_init(&st.clients_mu, NULL);
    st.client_fds_cap = 64; st.client_fds_count = 0; st.client_fds = (int*)calloc((size_t)st.client_fds_cap, sizeof(int));

    int client_threads = 4; st.client_thread_count = client_threads;
    st.client_threads = (pthread_t*)calloc((size_t)client_threads, sizeof(pthread_t));
    for (int i = 0; i < client_threads; i++) pthread_create(&st.client_threads[i], NULL, client_thread_main, &st);

    worker_pool_start(&st.worker_pool, &st.task_queue, 4, st.root_dir, &st.db, st.locks);

    int lfd = create_listener(port);
    fprintf(stdout, "Server listening on %d\n", port);
    while (g_running) {
        struct sockaddr_in cli; socklen_t cl = sizeof(cli);
        int cfd = accept(lfd, (struct sockaddr*)&cli, &cl);
        if (cfd < 0) { if (errno == EINTR) continue; perror("accept"); break; }
        char ip[64];
        const char *ipstr = inet_ntop(AF_INET, &cli.sin_addr, ip, sizeof(ip)) ? ip : "?";
        int cport = ntohs(cli.sin_port);
        fprintf(stdout, "Client connected %s:%d\n", ipstr, cport);
        pthread_mutex_lock(&st.clients_mu);
        if (st.client_fds_count == st.client_fds_cap) {
            st.client_fds_cap *= 2;
            st.client_fds = (int*)realloc(st.client_fds, (size_t)st.client_fds_cap * sizeof(int));
        }
        st.client_fds[st.client_fds_count++] = cfd;
        pthread_mutex_unlock(&st.clients_mu);
        ts_queue_push(&st.client_queue, (void*)(intptr_t)cfd);
    }
    close(lfd);

    ts_queue_close(&st.client_queue);
    // Proactively close active client sockets to unblock reads
    pthread_mutex_lock(&st.clients_mu);
    for (int i = 0; i < st.client_fds_count; i++) close(st.client_fds[i]);
    pthread_mutex_unlock(&st.clients_mu);
    for (int i = 0; i < st.client_thread_count; i++) pthread_join(st.client_threads[i], NULL);
    free(st.client_threads);

    worker_pool_stop(&st.worker_pool);
    ts_queue_destroy(&st.client_queue);
    ts_queue_destroy(&st.task_queue);
    db_close(&st.db);
    pthread_mutex_destroy(&st.clients_mu);
    free(st.client_fds);
    lockmgr_destroy(st.locks);
    (void)default_quota; // currently default quota applies on signup
    return 0;
}


