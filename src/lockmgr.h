#ifndef LOCKMGR_H
#define LOCKMGR_H

#include <pthread.h>

typedef struct lockmgr lockmgr_t;

int lockmgr_init(lockmgr_t **out);
void lockmgr_destroy(lockmgr_t *lm);

// Acquire per-user lock: write=1 for mutating ops; write=0 for readers like LIST
void lockmgr_user_lock(lockmgr_t *lm, const char *username, int write);
void lockmgr_user_unlock(lockmgr_t *lm, const char *username, int write);

// Acquire per-file lock under a user: write=1 for upload/delete; read=0 for download
void lockmgr_file_lock(lockmgr_t *lm, const char *username, const char *filename, int write);
void lockmgr_file_unlock(lockmgr_t *lm, const char *username, const char *filename, int write);

#endif


