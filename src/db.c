#include <stdlib.h>
#include <string.h>
#include "db.h"
#include "item.h"
#include "ring.h"
#include "sqlite3.h"
#include "tinycthread.h"
#include <sglib.h>

static int db_enabled = 0;

static sqlite3 *db;
static sqlite3_stmt *insert_block_stmt;
static sqlite3_stmt *insert_item_stmt;
static sqlite3_stmt *insert_light_stmt;
static sqlite3_stmt *insert_sign_stmt;
static sqlite3_stmt *delete_sign_stmt;
static sqlite3_stmt *delete_signs_stmt;
static sqlite3_stmt *load_blocks_stmt;
static sqlite3_stmt *check_item_exists_stmt;
static sqlite3_stmt *load_items_stmt;
static sqlite3_stmt *load_lights_stmt;
static sqlite3_stmt *load_signs_stmt;
static sqlite3_stmt *get_key_stmt;
static sqlite3_stmt *set_key_stmt;

static Ring ring;
static thrd_t thrd;
static mtx_t mtx;
static cnd_t cnd;
static mtx_t load_mtx;

struct db_item_list {
    const char *name;
    int id;

    struct db_item_list *next_ptr;
};

static struct db_item_list *db_items;

// holds the highest item id we have in our db
// this gets recalced every time we add or subtract items
int last_db_item_id;

void _recalc_last_db_item_id() {
    last_db_item_id = 0;

    struct db_item_list *it = NULL;
    SGLIB_LIST_MAP_ON_ELEMENTS(struct db_item_list, db_items, it, next_ptr, {
        if (last_db_item_id < it->id) {
            last_db_item_id = it->id;
        }
    });
}

// we do lots and lots of lookups for ids, when loading or saving from the database
// doing this by db access is slow, so use an index-based cache similar to how the
//   item ids are cached to access things much faster
int *_db_to_id = NULL;
int *_id_to_db = NULL;

// recalculate the id-based lookup caches
void _recalc_db_item_caches() {
    // free old space
    if (_db_to_id != NULL) {
        free(_db_to_id);
    }
    if (_id_to_db != NULL) {
        free(_id_to_db);
    }

    // create new space
    _db_to_id = (int *)calloc(sizeof(int), last_db_item_id + 1);
    _id_to_db = (int *)calloc(sizeof(int), last_item_id + 1);

    // and fill in the id mappings
    struct db_item_list *it = NULL;
    struct item_list *item = NULL;
    int db_id = 0;
    int item_id = 0;
    SGLIB_LIST_MAP_ON_ELEMENTS(struct db_item_list, db_items, it, next_ptr, {
        db_id = it->id;
        item = get_item_by_name(it->name);
        if (item == NULL) {
            item_id = 0;
        } else {
            item_id = item->id;
        }
        _db_to_id[db_id] = item_id;
        _id_to_db[item_id] = db_id;
    });
}

int item_db_to_id(unsigned int id) {
    // cache is only valid for ids <= last_item_id, so we check that here
    if (id <= last_db_item_id) {
        return _db_to_id[id];
    }
    return 0;
}

int item_id_to_db(unsigned int id) {
    // cache is only valid for ids <= last_item_id, so we check that here
    if (id <= last_item_id) {
        return _id_to_db[id];
    }
    return 0;
}

void db_enable() {
    db_enabled = 1;
}

void db_disable() {
    db_enabled = 0;
}

int get_db_enabled() {
    return db_enabled;
}

int db_init(char *path) {
    if (!db_enabled) {
        return 0;
    }
    static const char *create_query =
        "attach database 'auth.db' as auth;"
        "create table if not exists auth.identity_token ("
        "   username text not null,"
        "   token text not null,"
        "   selected int not null"
        ");"
        "create unique index if not exists auth.identity_token_username_idx"
        "   on identity_token (username);"
        "create table if not exists state ("
        "   x float not null,"
        "   y float not null,"
        "   z float not null,"
        "   rx float not null,"
        "   ry float not null"
        ");"
        "create table if not exists block ("
        "    p int not null,"
        "    q int not null,"
        "    x int not null,"
        "    y int not null,"
        "    z int not null,"
        "    w int not null"
        ");"
        "create table if not exists items ("
        "    id int not null,"
        "    name text not null"
        ");"
        "create table if not exists light ("
        "    p int not null,"
        "    q int not null,"
        "    x int not null,"
        "    y int not null,"
        "    z int not null,"
        "    w int not null"
        ");"
        "create table if not exists key ("
        "    p int not null,"
        "    q int not null,"
        "    key int not null"
        ");"
        "create table if not exists sign ("
        "    p int not null,"
        "    q int not null,"
        "    x int not null,"
        "    y int not null,"
        "    z int not null,"
        "    face int not null,"
        "    text text not null"
        ");"
        "create unique index if not exists block_pqxyz_idx on block (p, q, x, y, z);"
        "create unique index if not exists items_id_idx on items (id);"
        "create unique index if not exists light_pqxyz_idx on light (p, q, x, y, z);"
        "create unique index if not exists key_pq_idx on key (p, q);"
        "create unique index if not exists sign_xyzface_idx on sign (x, y, z, face);"
        "create index if not exists sign_pq_idx on sign (p, q);";
    static const char *insert_block_query =
        "insert or replace into block (p, q, x, y, z, w) "
        "values (?, ?, ?, ?, ?, ?);";
    static const char *insert_item_query =
        "insert or replace into items (id, name) "
        "values (?, ?);";
    static const char *insert_light_query =
        "insert or replace into light (p, q, x, y, z, w) "
        "values (?, ?, ?, ?, ?, ?);";
    static const char *insert_sign_query =
        "insert or replace into sign (p, q, x, y, z, face, text) "
        "values (?, ?, ?, ?, ?, ?, ?);";
    static const char *delete_sign_query =
        "delete from sign where x = ? and y = ? and z = ? and face = ?;";
    static const char *delete_signs_query =
        "delete from sign where x = ? and y = ? and z = ?;";
    static const char *load_blocks_query =
        "select x, y, z, w from block where p = ? and q = ?;";
    static const char *check_item_exists_query =
        "select 1 from items where name = ?;";
    static const char *load_items_query =
        "select id, name from items;";
    static const char *load_lights_query =
        "select x, y, z, w from light where p = ? and q = ?;";
    static const char *load_signs_query =
        "select x, y, z, face, text from sign where p = ? and q = ?;";
    static const char *get_key_query =
        "select key from key where p = ? and q = ?;";
    static const char *set_key_query =
        "insert or replace into key (p, q, key) "
        "values (?, ?, ?);";
    int rc;
    rc = sqlite3_open(path, &db);
    if (rc) return rc;
    rc = sqlite3_exec(db, create_query, NULL, NULL, NULL);
    if (rc) return rc;
    rc = sqlite3_prepare_v2(
        db, insert_block_query, -1, &insert_block_stmt, NULL);
    if (rc) return rc;
    rc = sqlite3_prepare_v2(
        db, insert_item_query, -1, &insert_item_stmt, NULL);
    if (rc) return rc;
    rc = sqlite3_prepare_v2(
        db, insert_light_query, -1, &insert_light_stmt, NULL);
    if (rc) return rc;
    rc = sqlite3_prepare_v2(
        db, insert_sign_query, -1, &insert_sign_stmt, NULL);
    if (rc) return rc;
    rc = sqlite3_prepare_v2(
        db, delete_sign_query, -1, &delete_sign_stmt, NULL);
    if (rc) return rc;
    rc = sqlite3_prepare_v2(
        db, delete_signs_query, -1, &delete_signs_stmt, NULL);
    if (rc) return rc;
    rc = sqlite3_prepare_v2(db, load_blocks_query, -1, &load_blocks_stmt, NULL);
    if (rc) return rc;
    rc = sqlite3_prepare_v2(db, check_item_exists_query, -1, &check_item_exists_stmt, NULL);
    if (rc) return rc;
    rc = sqlite3_prepare_v2(db, load_items_query, -1, &load_items_stmt, NULL);
    if (rc) return rc;
    rc = sqlite3_prepare_v2(db, load_lights_query, -1, &load_lights_stmt, NULL);
    if (rc) return rc;
    rc = sqlite3_prepare_v2(db, load_signs_query, -1, &load_signs_stmt, NULL);
    if (rc) return rc;
    rc = sqlite3_prepare_v2(db, get_key_query, -1, &get_key_stmt, NULL);
    if (rc) return rc;
    rc = sqlite3_prepare_v2(db, set_key_query, -1, &set_key_stmt, NULL);
    if (rc) return rc;
    sqlite3_exec(db, "begin;", NULL, NULL, NULL);
    db_worker_start();

    db_load_items();

    return 0;
}

void db_close() {
    if (!db_enabled) {
        return;
    }
    db_worker_stop();
    sqlite3_exec(db, "commit;", NULL, NULL, NULL);
    sqlite3_finalize(insert_block_stmt);
    sqlite3_finalize(insert_item_stmt);
    sqlite3_finalize(insert_light_stmt);
    sqlite3_finalize(insert_sign_stmt);
    sqlite3_finalize(delete_sign_stmt);
    sqlite3_finalize(delete_signs_stmt);
    sqlite3_finalize(load_blocks_stmt);
    sqlite3_finalize(check_item_exists_stmt);
    sqlite3_finalize(load_items_stmt);
    sqlite3_finalize(load_lights_stmt);
    sqlite3_finalize(load_signs_stmt);
    sqlite3_finalize(get_key_stmt);
    sqlite3_finalize(set_key_stmt);
    sqlite3_close(db);
}

void db_commit() {
    if (!db_enabled) {
        return;
    }
    mtx_lock(&mtx);
    ring_put_commit(&ring);
    cnd_signal(&cnd);
    mtx_unlock(&mtx);
}

void _db_commit() {
    sqlite3_exec(db, "commit; begin;", NULL, NULL, NULL);
}

void db_auth_set(char *username, char *identity_token) {
    if (!db_enabled) {
        return;
    }
    static const char *query =
        "insert or replace into auth.identity_token "
        "(username, token, selected) values (?, ?, ?);";
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, query, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, username, -1, NULL);
    sqlite3_bind_text(stmt, 2, identity_token, -1, NULL);
    sqlite3_bind_int(stmt, 3, 1);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    db_auth_select(username);
}

int db_auth_select(char *username) {
    if (!db_enabled) {
        return 0;
    }
    db_auth_select_none();
    static const char *query =
        "update auth.identity_token set selected = 1 where username = ?;";
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, query, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, username, -1, NULL);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return sqlite3_changes(db);
}

void db_auth_select_none() {
    if (!db_enabled) {
        return;
    }
    sqlite3_exec(db, "update auth.identity_token set selected = 0;",
        NULL, NULL, NULL);
}

int db_auth_get(
    char *username,
    char *identity_token, int identity_token_length)
{
    if (!db_enabled) {
        return 0;
    }
    static const char *query =
        "select token from auth.identity_token "
        "where username = ?;";
    int result = 0;
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, query, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, username, -1, NULL);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *a = (const char *)sqlite3_column_text(stmt, 0);
        strncpy(identity_token, a, identity_token_length - 1);
        identity_token[identity_token_length - 1] = '\0';
        result = 1;
    }
    sqlite3_finalize(stmt);
    return result;
}

int db_auth_get_selected(
    char *username, int username_length,
    char *identity_token, int identity_token_length)
{
    if (!db_enabled) {
        return 0;
    }
    static const char *query =
        "select username, token from auth.identity_token "
        "where selected = 1;";
    int result = 0;
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, query, -1, &stmt, NULL);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *a = (const char *)sqlite3_column_text(stmt, 0);
        const char *b = (const char *)sqlite3_column_text(stmt, 1);
        strncpy(username, a, username_length - 1);
        username[username_length - 1] = '\0';
        strncpy(identity_token, b, identity_token_length - 1);
        identity_token[identity_token_length - 1] = '\0';
        result = 1;
    }
    sqlite3_finalize(stmt);
    return result;
}

void db_save_state(float x, float y, float z, float rx, float ry) {
    if (!db_enabled) {
        return;
    }
    static const char *query =
        "insert into state (x, y, z, rx, ry) values (?, ?, ?, ?, ?);";
    sqlite3_stmt *stmt;
    sqlite3_exec(db, "delete from state;", NULL, NULL, NULL);
    sqlite3_prepare_v2(db, query, -1, &stmt, NULL);
    sqlite3_bind_double(stmt, 1, x);
    sqlite3_bind_double(stmt, 2, y);
    sqlite3_bind_double(stmt, 3, z);
    sqlite3_bind_double(stmt, 4, rx);
    sqlite3_bind_double(stmt, 5, ry);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

int db_load_state(float *x, float *y, float *z, float *rx, float *ry) {
    if (!db_enabled) {
        return 0;
    }
    static const char *query =
        "select x, y, z, rx, ry from state;";
    int result = 0;
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, query, -1, &stmt, NULL);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        *x = sqlite3_column_double(stmt, 0);
        *y = sqlite3_column_double(stmt, 1);
        *z = sqlite3_column_double(stmt, 2);
        *rx = sqlite3_column_double(stmt, 3);
        *ry = sqlite3_column_double(stmt, 4);
        result = 1;
    }
    sqlite3_finalize(stmt);
    return result;
}

void db_insert_block(int p, int q, int x, int y, int z, int w) {
    if (!db_enabled) {
        return;
    }
    mtx_lock(&mtx);
    ring_put_block(&ring, p, q, x, y, z, w);
    cnd_signal(&cnd);
    mtx_unlock(&mtx);
}

bool db_item_exists(const char *name) {
    // return true if item exists in db, false otherwise
    mtx_lock(&load_mtx);
    sqlite3_reset(check_item_exists_stmt);
    sqlite3_bind_text(check_item_exists_stmt, 1, name, -1, NULL);
    bool exists = false;
    while (sqlite3_step(check_item_exists_stmt) == SQLITE_ROW) {
        exists = sqlite3_column_int(check_item_exists_stmt, 0);
    }
    mtx_unlock(&load_mtx);

    return exists;
}

void db_insert_item(int id, const char *name) {
    if (!db_enabled) {
        return;
    }
    if (!db_item_exists(name)) {
        int new_db_item_id = last_db_item_id + 1;

        // add to our db
        sqlite3_reset(insert_item_stmt);
        sqlite3_bind_int(insert_item_stmt, 1, new_db_item_id);
        sqlite3_bind_text(insert_item_stmt, 2, name, -1, NULL);
        sqlite3_step(insert_item_stmt);

        // and to our internal list
        struct db_item_list *new_db_item = malloc(sizeof(struct db_item_list));
        new_db_item->id = new_db_item_id;
        new_db_item->name = name;
        SGLIB_LIST_ADD(struct db_item_list, db_items, new_db_item, next_ptr);

        _recalc_last_db_item_id();
    }
    _recalc_db_item_caches();
}

void _db_insert_block(int p, int q, int x, int y, int z, int w) {
    sqlite3_reset(insert_block_stmt);
    sqlite3_bind_int(insert_block_stmt, 1, p);
    sqlite3_bind_int(insert_block_stmt, 2, q);
    sqlite3_bind_int(insert_block_stmt, 3, x);
    sqlite3_bind_int(insert_block_stmt, 4, y);
    sqlite3_bind_int(insert_block_stmt, 5, z);
    sqlite3_bind_int(insert_block_stmt, 6, w);
    sqlite3_step(insert_block_stmt);
}

void db_insert_light(int p, int q, int x, int y, int z, int w) {
    if (!db_enabled) {
        return;
    }
    mtx_lock(&mtx);
    ring_put_light(&ring, p, q, x, y, z, w);
    cnd_signal(&cnd);
    mtx_unlock(&mtx);
}

void _db_insert_light(int p, int q, int x, int y, int z, int w) {
    sqlite3_reset(insert_light_stmt);
    sqlite3_bind_int(insert_light_stmt, 1, p);
    sqlite3_bind_int(insert_light_stmt, 2, q);
    sqlite3_bind_int(insert_light_stmt, 3, x);
    sqlite3_bind_int(insert_light_stmt, 4, y);
    sqlite3_bind_int(insert_light_stmt, 5, z);
    sqlite3_bind_int(insert_light_stmt, 6, w);
    sqlite3_step(insert_light_stmt);
}

void db_insert_sign(
    int p, int q, int x, int y, int z, int face, const char *text)
{
    if (!db_enabled) {
        return;
    }
    sqlite3_reset(insert_sign_stmt);
    sqlite3_bind_int(insert_sign_stmt, 1, p);
    sqlite3_bind_int(insert_sign_stmt, 2, q);
    sqlite3_bind_int(insert_sign_stmt, 3, x);
    sqlite3_bind_int(insert_sign_stmt, 4, y);
    sqlite3_bind_int(insert_sign_stmt, 5, z);
    sqlite3_bind_int(insert_sign_stmt, 6, face);
    sqlite3_bind_text(insert_sign_stmt, 7, text, -1, NULL);
    sqlite3_step(insert_sign_stmt);
}

void db_delete_sign(int x, int y, int z, int face) {
    if (!db_enabled) {
        return;
    }
    sqlite3_reset(delete_sign_stmt);
    sqlite3_bind_int(delete_sign_stmt, 1, x);
    sqlite3_bind_int(delete_sign_stmt, 2, y);
    sqlite3_bind_int(delete_sign_stmt, 3, z);
    sqlite3_bind_int(delete_sign_stmt, 4, face);
    sqlite3_step(delete_sign_stmt);
}

void db_delete_signs(int x, int y, int z) {
    if (!db_enabled) {
        return;
    }
    sqlite3_reset(delete_signs_stmt);
    sqlite3_bind_int(delete_signs_stmt, 1, x);
    sqlite3_bind_int(delete_signs_stmt, 2, y);
    sqlite3_bind_int(delete_signs_stmt, 3, z);
    sqlite3_step(delete_signs_stmt);
}

void db_delete_all_signs() {
    if (!db_enabled) {
        return;
    }
    sqlite3_exec(db, "delete from sign;", NULL, NULL, NULL);
}

void db_load_blocks(Map *map, int p, int q) {
    if (!db_enabled) {
        return;
    }
    mtx_lock(&load_mtx);
    sqlite3_reset(load_blocks_stmt);
    sqlite3_bind_int(load_blocks_stmt, 1, p);
    sqlite3_bind_int(load_blocks_stmt, 2, q);
    while (sqlite3_step(load_blocks_stmt) == SQLITE_ROW) {
        int x = sqlite3_column_int(load_blocks_stmt, 0);
        int y = sqlite3_column_int(load_blocks_stmt, 1);
        int z = sqlite3_column_int(load_blocks_stmt, 2);
        int w = sqlite3_column_int(load_blocks_stmt, 3);
        map_set(map, x, y, z, w);
    }
    mtx_unlock(&load_mtx);
}

void db_load_items() {
    if (!db_enabled) {
        return;
    }
    mtx_lock(&load_mtx);
    sqlite3_reset(load_items_stmt);
    db_items = NULL;
    while (sqlite3_step(load_items_stmt) == SQLITE_ROW) {
        struct db_item_list *new_db_item = malloc(sizeof(struct db_item_list));
        new_db_item->id = sqlite3_column_int(load_items_stmt, 0);
        new_db_item->name = (const char *)sqlite3_column_text(load_items_stmt, 1);
        SGLIB_LIST_ADD(struct db_item_list, db_items, new_db_item, next_ptr);
    }
    mtx_unlock(&load_mtx);
    _recalc_last_db_item_id();
    _recalc_db_item_caches();
}

void db_load_lights(Map *map, int p, int q) {
    if (!db_enabled) {
        return;
    }
    mtx_lock(&load_mtx);
    sqlite3_reset(load_lights_stmt);
    sqlite3_bind_int(load_lights_stmt, 1, p);
    sqlite3_bind_int(load_lights_stmt, 2, q);
    while (sqlite3_step(load_lights_stmt) == SQLITE_ROW) {
        int x = sqlite3_column_int(load_lights_stmt, 0);
        int y = sqlite3_column_int(load_lights_stmt, 1);
        int z = sqlite3_column_int(load_lights_stmt, 2);
        int w = sqlite3_column_int(load_lights_stmt, 3);
        map_set(map, x, y, z, w);
    }
    mtx_unlock(&load_mtx);
}

void db_load_signs(SignList *list, int p, int q) {
    if (!db_enabled) {
        return;
    }
    sqlite3_reset(load_signs_stmt);
    sqlite3_bind_int(load_signs_stmt, 1, p);
    sqlite3_bind_int(load_signs_stmt, 2, q);
    while (sqlite3_step(load_signs_stmt) == SQLITE_ROW) {
        int x = sqlite3_column_int(load_signs_stmt, 0);
        int y = sqlite3_column_int(load_signs_stmt, 1);
        int z = sqlite3_column_int(load_signs_stmt, 2);
        int face = sqlite3_column_int(load_signs_stmt, 3);
        const char *text = (const char *)sqlite3_column_text(
            load_signs_stmt, 4);
        sign_list_add(list, x, y, z, face, text);
    }
}

int db_get_key(int p, int q) {
    if (!db_enabled) {
        return 0;
    }
    sqlite3_reset(get_key_stmt);
    sqlite3_bind_int(get_key_stmt, 1, p);
    sqlite3_bind_int(get_key_stmt, 2, q);
    if (sqlite3_step(get_key_stmt) == SQLITE_ROW) {
        return sqlite3_column_int(get_key_stmt, 0);
    }
    return 0;
}

void db_set_key(int p, int q, int key) {
    if (!db_enabled) {
        return;
    }
    mtx_lock(&mtx);
    ring_put_key(&ring, p, q, key);
    cnd_signal(&cnd);
    mtx_unlock(&mtx);
}

void _db_set_key(int p, int q, int key) {
    sqlite3_reset(set_key_stmt);
    sqlite3_bind_int(set_key_stmt, 1, p);
    sqlite3_bind_int(set_key_stmt, 2, q);
    sqlite3_bind_int(set_key_stmt, 3, key);
    sqlite3_step(set_key_stmt);
}

void db_worker_start(char *path) {
    if (!db_enabled) {
        return;
    }
    ring_alloc(&ring, 1024);
    mtx_init(&mtx, mtx_plain);
    mtx_init(&load_mtx, mtx_plain);
    cnd_init(&cnd);
    thrd_create(&thrd, db_worker_run, path);
}

void db_worker_stop() {
    if (!db_enabled) {
        return;
    }
    mtx_lock(&mtx);
    ring_put_exit(&ring);
    cnd_signal(&cnd);
    mtx_unlock(&mtx);
    thrd_join(thrd, NULL);
    cnd_destroy(&cnd);
    mtx_destroy(&load_mtx);
    mtx_destroy(&mtx);
    ring_free(&ring);
}

int db_worker_run(void *arg) {
    int running = 1;
    while (running) {
        RingEntry e;
        mtx_lock(&mtx);
        while (!ring_get(&ring, &e)) {
            cnd_wait(&cnd, &mtx);
        }
        mtx_unlock(&mtx);
        switch (e.type) {
            case BLOCK:
                _db_insert_block(e.p, e.q, e.x, e.y, e.z, e.w);
                break;
            case LIGHT:
                _db_insert_light(e.p, e.q, e.x, e.y, e.z, e.w);
                break;
            case KEY:
                _db_set_key(e.p, e.q, e.key);
                break;
            case COMMIT:
                _db_commit();
                break;
            case EXIT:
                running = 0;
                break;
        }
    }
    return 0;
}
