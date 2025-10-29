#include "db.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int exec_sql(sqlite3 *db, const char *sql) {
    char *errmsg = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        if (errmsg) sqlite3_free(errmsg);
        return -1;
    }
    return 0;
}

int db_open(db_t *db, const char *path) {
    if (sqlite3_open(path, &db->conn) != SQLITE_OK) return -1;
    exec_sql(db->conn, "PRAGMA journal_mode=WAL;");
    exec_sql(db->conn, "PRAGMA foreign_keys=ON;");
    exec_sql(db->conn, "PRAGMA busy_timeout=5000;");
    const char *schema =
        "CREATE TABLE IF NOT EXISTS users(" \
        " id INTEGER PRIMARY KEY, username TEXT UNIQUE, pass_hash TEXT," \
        " quota_bytes INTEGER, used_bytes INTEGER DEFAULT 0, created_at INTEGER);" \
        "CREATE TABLE IF NOT EXISTS files(" \
        " id INTEGER PRIMARY KEY, user_id INTEGER, name TEXT, size INTEGER, created_at INTEGER,"
        " UNIQUE(user_id,name), FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);" \
        "CREATE INDEX IF NOT EXISTS files_user_name ON files(user_id,name);";
    if (exec_sql(db->conn, schema) != 0) return -1;
    return 0;
}

void db_close(db_t *db) {
    if (db->conn) sqlite3_close(db->conn);
    db->conn = NULL;
}

int db_signup(db_t *db, const char *username, const char *pass_hash, long long quota_bytes) {
    const char *sql = "INSERT INTO users(username, pass_hash, quota_bytes, created_at) VALUES(?,?,?,strftime('%s','now'))";
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db->conn, sql, -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, username, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, pass_hash, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 3, quota_bytes);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return -1;
    return 0;
}

int db_get_user(db_t *db, const char *username, long long *out_user_id, char **out_pass_hash, long long *out_quota, long long *out_used) {
    const char *sql = "SELECT id, pass_hash, quota_bytes, used_bytes FROM users WHERE username=?";
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db->conn, sql, -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, username, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(st);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(st);
        return -1;
    }
    if (out_user_id) *out_user_id = sqlite3_column_int64(st, 0);
    if (out_pass_hash) {
        const unsigned char *ph = sqlite3_column_text(st, 1);
        *out_pass_hash = ph ? strdup((const char*)ph) : NULL;
    }
    if (out_quota) *out_quota = sqlite3_column_int64(st, 2);
    if (out_used) *out_used = sqlite3_column_int64(st, 3);
    sqlite3_finalize(st);
    return 0;
}

int db_list_files(db_t *db, long long user_id, char ***out_names, int *out_count) {
    *out_names = NULL; *out_count = 0;
    const char *sql = "SELECT name FROM files WHERE user_id=? ORDER BY name";
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db->conn, sql, -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int64(st, 1, user_id);
    int cap = 8;
    char **names = (char**)malloc(sizeof(char*) * (size_t)cap);
    int n = 0;
    int rc;
    while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
        const unsigned char *name = sqlite3_column_text(st, 0);
        if (n == cap) {
            cap *= 2;
            names = (char**)realloc(names, sizeof(char*) * (size_t)cap);
        }
        names[n++] = strdup((const char*)name);
    }
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) {
        for (int i = 0; i < n; i++) free(names[i]);
        free(names);
        return -1;
    }
    *out_names = names;
    *out_count = n;
    return 0;
}

int db_get_file_size(db_t *db, long long user_id, const char *name, long long *out_size) {
    const char *sql = "SELECT size FROM files WHERE user_id=? AND name=?";
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db->conn, sql, -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int64(st, 1, user_id);
    sqlite3_bind_text(st, 2, name, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(st);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(st);
        return -1;
    }
    *out_size = sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);
    return 0;
}

int db_upsert_file(db_t *db, long long user_id, const char *name, long long new_size, long long *delta_used) {
    int rc = 0;
    sqlite3_exec(db->conn, "BEGIN IMMEDIATE", NULL, NULL, NULL);
    long long old_size = 0;
    {
        const char *sqls = "SELECT size FROM files WHERE user_id=? AND name=?";
        sqlite3_stmt *st = NULL;
        if (sqlite3_prepare_v2(db->conn, sqls, -1, &st, NULL) != SQLITE_OK) { rc = -1; goto end; }
        sqlite3_bind_int64(st, 1, user_id);
        sqlite3_bind_text(st, 2, name, -1, SQLITE_TRANSIENT);
        int stepr = sqlite3_step(st);
        if (stepr == SQLITE_ROW) old_size = sqlite3_column_int64(st, 0);
        sqlite3_finalize(st);
    }
    {
        const char *sqlu = "INSERT INTO files(user_id,name,size,created_at) VALUES(?,?,?,strftime('%s','now')) "
                           "ON CONFLICT(user_id,name) DO UPDATE SET size=excluded.size";
        sqlite3_stmt *st = NULL;
        if (sqlite3_prepare_v2(db->conn, sqlu, -1, &st, NULL) != SQLITE_OK) { rc = -1; goto end; }
        sqlite3_bind_int64(st, 1, user_id);
        sqlite3_bind_text(st, 2, name, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(st, 3, new_size);
        if (sqlite3_step(st) != SQLITE_DONE) { sqlite3_finalize(st); rc = -1; goto end; }
        sqlite3_finalize(st);
    }
    {
        long long delta = new_size - old_size;
        if (delta_used) *delta_used = delta;
        const char *sqlu = "UPDATE users SET used_bytes=used_bytes+? WHERE id=?";
        sqlite3_stmt *st = NULL;
        if (sqlite3_prepare_v2(db->conn, sqlu, -1, &st, NULL) != SQLITE_OK) { rc = -1; goto end; }
        sqlite3_bind_int64(st, 1, delta);
        sqlite3_bind_int64(st, 2, user_id);
        if (sqlite3_step(st) != SQLITE_DONE) { sqlite3_finalize(st); rc = -1; goto end; }
        sqlite3_finalize(st);
    }
end:
    if (rc == 0) sqlite3_exec(db->conn, "COMMIT", NULL, NULL, NULL);
    else sqlite3_exec(db->conn, "ROLLBACK", NULL, NULL, NULL);
    return rc;
}

int db_delete_file(db_t *db, long long user_id, const char *name, long long *size_deleted) {
    int rc = 0;
    sqlite3_exec(db->conn, "BEGIN IMMEDIATE", NULL, NULL, NULL);
    long long sz = 0;
    {
        const char *sqls = "SELECT size FROM files WHERE user_id=? AND name=?";
        sqlite3_stmt *st = NULL;
        if (sqlite3_prepare_v2(db->conn, sqls, -1, &st, NULL) != SQLITE_OK) { rc = -1; goto end; }
        sqlite3_bind_int64(st, 1, user_id);
        sqlite3_bind_text(st, 2, name, -1, SQLITE_TRANSIENT);
        int stepr = sqlite3_step(st);
        if (stepr == SQLITE_ROW) sz = sqlite3_column_int64(st, 0);
        else { sqlite3_finalize(st); rc = -1; goto end; }
        sqlite3_finalize(st);
    }
    {
        const char *sqld = "DELETE FROM files WHERE user_id=? AND name=?";
        sqlite3_stmt *st = NULL;
        if (sqlite3_prepare_v2(db->conn, sqld, -1, &st, NULL) != SQLITE_OK) { rc = -1; goto end; }
        sqlite3_bind_int64(st, 1, user_id);
        sqlite3_bind_text(st, 2, name, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(st) != SQLITE_DONE) { sqlite3_finalize(st); rc = -1; goto end; }
        sqlite3_finalize(st);
    }
    {
        const char *sqlu = "UPDATE users SET used_bytes=used_bytes-? WHERE id=?";
        sqlite3_stmt *st = NULL;
        if (sqlite3_prepare_v2(db->conn, sqlu, -1, &st, NULL) != SQLITE_OK) { rc = -1; goto end; }
        sqlite3_bind_int64(st, 1, sz);
        sqlite3_bind_int64(st, 2, user_id);
        if (sqlite3_step(st) != SQLITE_DONE) { sqlite3_finalize(st); rc = -1; goto end; }
        sqlite3_finalize(st);
    }
end:
    if (rc == 0) sqlite3_exec(db->conn, "COMMIT", NULL, NULL, NULL);
    else sqlite3_exec(db->conn, "ROLLBACK", NULL, NULL, NULL);
    if (rc == 0 && size_deleted) *size_deleted = sz;
    return rc;
}

int db_adjust_used_bytes(db_t *db, long long user_id, long long delta, int check_quota) {
    int rc = 0;
    sqlite3_exec(db->conn, "BEGIN IMMEDIATE", NULL, NULL, NULL);
    long long quota = 0, used = 0;
    {
        const char *sql = "SELECT quota_bytes, used_bytes FROM users WHERE id=?";
        sqlite3_stmt *st = NULL;
        if (sqlite3_prepare_v2(db->conn, sql, -1, &st, NULL) != SQLITE_OK) { rc = -1; goto end; }
        sqlite3_bind_int64(st, 1, user_id);
        int stepr = sqlite3_step(st);
        if (stepr != SQLITE_ROW) { sqlite3_finalize(st); rc = -1; goto end; }
        quota = sqlite3_column_int64(st, 0);
        used = sqlite3_column_int64(st, 1);
        sqlite3_finalize(st);
    }
    if (check_quota && used + delta > quota) { rc = -1; goto end; }
    {
        const char *sqlu = "UPDATE users SET used_bytes=used_bytes+? WHERE id=?";
        sqlite3_stmt *st = NULL;
        if (sqlite3_prepare_v2(db->conn, sqlu, -1, &st, NULL) != SQLITE_OK) { rc = -1; goto end; }
        sqlite3_bind_int64(st, 1, delta);
        sqlite3_bind_int64(st, 2, user_id);
        if (sqlite3_step(st) != SQLITE_DONE) { sqlite3_finalize(st); rc = -1; goto end; }
        sqlite3_finalize(st);
    }
end:
    if (rc == 0) sqlite3_exec(db->conn, "COMMIT", NULL, NULL, NULL);
    else sqlite3_exec(db->conn, "ROLLBACK", NULL, NULL, NULL);
    return rc;
}


