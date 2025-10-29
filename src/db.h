#ifndef DB_H
#define DB_H

#include <sqlite3.h>

typedef struct {
    sqlite3 *conn;
} db_t;

int db_open(db_t *db, const char *path); // initializes schema, WAL
void db_close(db_t *db);

// returns 0 on success, -1 on conflict
int db_signup(db_t *db, const char *username, const char *pass_hash, long long quota_bytes);

// returns 0 on success; -1 on not found or wrong password (caller compares hash)
int db_get_user(db_t *db, const char *username, long long *out_user_id, char **out_pass_hash, long long *out_quota, long long *out_used);

// file metadata ops
int db_list_files(db_t *db, long long user_id, char ***out_names, int *out_count);
int db_get_file_size(db_t *db, long long user_id, const char *name, long long *out_size);
int db_upsert_file(db_t *db, long long user_id, const char *name, long long new_size, long long *delta_used);
int db_delete_file(db_t *db, long long user_id, const char *name, long long *size_deleted);

// updates used_bytes by delta; checks quota if check_quota != 0; returns -1 if exceeds
int db_adjust_used_bytes(db_t *db, long long user_id, long long delta, int check_quota);

#endif


