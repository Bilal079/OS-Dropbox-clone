#include "lockmgr.h"

#include <stdlib.h>
#include <string.h>

typedef struct lock_entry {
    char *key;
    pthread_rwlock_t rw;
    int refcnt;
    struct lock_entry *next;
} lock_entry_t;

struct lockmgr {
    pthread_mutex_t mu;
    lock_entry_t **buckets;
    size_t nbuckets;
};

static unsigned long hash_str(const char *s) {
    unsigned long h = 1469598103934665603ULL;
    while (*s) { h ^= (unsigned char)(*s++); h *= 1099511628211ULL; }
    return h;
}

static char *make_user_key(const char *u) {
    size_t n = strlen(u);
    char *k = (char*)malloc(n + 3);
    if (!k) return NULL;
    k[0] = 'U'; k[1] = ':'; memcpy(k+2, u, n+1);
    return k;
}

static char *make_file_key(const char *u, const char *f) {
    size_t nu = strlen(u), nf = strlen(f);
    char *k = (char*)malloc(nu + nf + 5);
    if (!k) return NULL;
    k[0] = 'F'; k[1] = ':';
    memcpy(k+2, u, nu); k[2+nu] = '|';
    memcpy(k+3+nu, f, nf+1);
    return k;
}

int lockmgr_init(lockmgr_t **out) {
    lockmgr_t *lm = (lockmgr_t*)calloc(1, sizeof(*lm));
    if (!lm) return -1;
    lm->nbuckets = 256;
    lm->buckets = (lock_entry_t**)calloc(lm->nbuckets, sizeof(lock_entry_t*));
    if (!lm->buckets) { free(lm); return -1; }
    pthread_mutex_init(&lm->mu, NULL);
    *out = lm;
    return 0;
}

static lock_entry_t *get_or_create(lockmgr_t *lm, const char *key) {
    unsigned long h = hash_str(key);
    size_t idx = h % lm->nbuckets;
    lock_entry_t *e = lm->buckets[idx];
    for (; e; e = e->next) {
        if (strcmp(e->key, key) == 0) {
            e->refcnt++;
            return e;
        }
    }
    e = (lock_entry_t*)calloc(1, sizeof(*e));
    if (!e) return NULL;
    e->key = strdup(key);
    pthread_rwlock_init(&e->rw, NULL);
    e->refcnt = 1;
    e->next = lm->buckets[idx];
    lm->buckets[idx] = e;
    return e;
}

static void release(lockmgr_t *lm, const char *key) {
    unsigned long h = hash_str(key);
    size_t idx = h % lm->nbuckets;
    lock_entry_t *prev = NULL, *e = lm->buckets[idx];
    while (e) {
        if (strcmp(e->key, key) == 0) {
            if (--e->refcnt == 0) {
                if (prev) prev->next = e->next; else lm->buckets[idx] = e->next;
                pthread_rwlock_destroy(&e->rw);
                free(e->key);
                free(e);
            }
            return;
        }
        prev = e; e = e->next;
    }
}

void lockmgr_destroy(lockmgr_t *lm) {
    if (!lm) return;
    for (size_t i = 0; i < lm->nbuckets; i++) {
        lock_entry_t *e = lm->buckets[i];
        while (e) {
            lock_entry_t *n = e->next;
            pthread_rwlock_destroy(&e->rw);
            free(e->key);
            free(e);
            e = n;
        }
    }
    free(lm->buckets);
    pthread_mutex_destroy(&lm->mu);
    free(lm);
}

void lockmgr_user_lock(lockmgr_t *lm, const char *username, int write) {
    char *k = make_user_key(username);
    pthread_mutex_lock(&lm->mu);
    lock_entry_t *e = get_or_create(lm, k);
    pthread_mutex_unlock(&lm->mu);
    free(k);
    if (!e) return;
    if (write) pthread_rwlock_wrlock(&e->rw); else pthread_rwlock_rdlock(&e->rw);
}

void lockmgr_user_unlock(lockmgr_t *lm, const char *username, int write) {
    (void)write;
    char *k = make_user_key(username);
    pthread_mutex_lock(&lm->mu);
    unsigned long h = hash_str(k);
    size_t idx = h % lm->nbuckets;
    lock_entry_t *e = lm->buckets[idx];
    while (e && strcmp(e->key, k) != 0) e = e->next;
    pthread_mutex_unlock(&lm->mu);
    if (e) pthread_rwlock_unlock(&e->rw);
    pthread_mutex_lock(&lm->mu);
    release(lm, k);
    pthread_mutex_unlock(&lm->mu);
    free(k);
}

void lockmgr_file_lock(lockmgr_t *lm, const char *username, const char *filename, int write) {
    char *k = make_file_key(username, filename);
    pthread_mutex_lock(&lm->mu);
    lock_entry_t *e = get_or_create(lm, k);
    pthread_mutex_unlock(&lm->mu);
    free(k);
    if (!e) return;
    if (write) pthread_rwlock_wrlock(&e->rw); else pthread_rwlock_rdlock(&e->rw);
}

void lockmgr_file_unlock(lockmgr_t *lm, const char *username, const char *filename, int write) {
    (void)write;
    char *k = make_file_key(username, filename);
    pthread_mutex_lock(&lm->mu);
    unsigned long h = hash_str(k);
    size_t idx = h % lm->nbuckets;
    lock_entry_t *e = lm->buckets[idx];
    while (e && strcmp(e->key, k) != 0) e = e->next;
    pthread_mutex_unlock(&lm->mu);
    if (e) pthread_rwlock_unlock(&e->rw);
    pthread_mutex_lock(&lm->mu);
    release(lm, k);
    pthread_mutex_unlock(&lm->mu);
    free(k);
}


