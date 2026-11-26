#include <string.h>
#include "db.h"

static void copy_text(char *dst, size_t size, const unsigned char *src)
{
    if (!src) { dst[0] = '\0'; return; }
    strncpy(dst, (const char *)src, size - 1);
    dst[size - 1] = '\0';
}

int db_open(sqlite3 **db, const char *path)
{
    if (sqlite3_open(path, db) != SQLITE_OK)
        return 1;

    const char *schema =
        "CREATE TABLE IF NOT EXISTS entries ("
        "  id           INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  date         TEXT    NOT NULL,"
        "  category     TEXT    NOT NULL,"
        "  description  TEXT    NOT NULL,"
        "  amount_cents INTEGER NOT NULL CHECK (amount_cents > 0),"
        "  kind         INTEGER NOT NULL CHECK (kind IN (0, 1))"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_entries_date ON entries(date);";

    return sqlite3_exec(*db, schema, NULL, NULL, NULL) == SQLITE_OK ? 0 : 1;
}

int db_add(sqlite3 *db, const Entry *e)
{
    sqlite3_stmt *st;
    const char *sql =
        "INSERT INTO entries (date, category, description, amount_cents, kind) "
        "VALUES (?, ?, ?, ?, ?);";

    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK)
        return 1;

    sqlite3_bind_text(st, 1, e->date, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, e->category, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, e->description, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 4, e->amount_cents);
    sqlite3_bind_int(st, 5, e->kind);

    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? 0 : 1;
}

int db_update(sqlite3 *db, const Entry *e, int *updated)
{
    sqlite3_stmt *st;
    const char *sql =
        "UPDATE entries SET date = ?, category = ?, description = ?, "
        "amount_cents = ?, kind = ? WHERE id = ?;";

    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK)
        return 1;

    sqlite3_bind_text(st, 1, e->date, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, e->category, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, e->description, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 4, e->amount_cents);
    sqlite3_bind_int(st, 5, e->kind);
    sqlite3_bind_int(st, 6, e->id);

    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE)
        return 1;

    *updated = sqlite3_changes(db);   /* 0 se o id não existe mais */
    return 0;
}

int db_delete(sqlite3 *db, int id, int *removed)
{
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(db, "DELETE FROM entries WHERE id = ?;", -1, &st, NULL) != SQLITE_OK)
        return 1;

    sqlite3_bind_int(st, 1, id);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE)
        return 1;

    /* changes() informa se o id realmente existia */
    sqlite3_stmt *ch;
    *removed = 0;
    if (sqlite3_prepare_v2(db, "SELECT changes();", -1, &ch, NULL) == SQLITE_OK) {
        if (sqlite3_step(ch) == SQLITE_ROW)
            *removed = sqlite3_column_int(ch, 0);
        sqlite3_finalize(ch);
    }
    return 0;
}

int db_list_month(sqlite3 *db, const char *ym, EntryCb cb, void *user)
{
    sqlite3_stmt *st;
    const char *sql =
        "SELECT id, date, category, description, amount_cents, kind "
        "FROM entries WHERE substr(date, 1, 7) = ? ORDER BY date, id;";

    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK)
        return 1;

    sqlite3_bind_text(st, 1, ym, -1, SQLITE_TRANSIENT);

    while (sqlite3_step(st) == SQLITE_ROW) {
        Entry e;
        e.id = sqlite3_column_int(st, 0);
        copy_text(e.date, sizeof e.date, sqlite3_column_text(st, 1));
        copy_text(e.category, sizeof e.category, sqlite3_column_text(st, 2));
        copy_text(e.description, sizeof e.description, sqlite3_column_text(st, 3));
        e.amount_cents = sqlite3_column_int64(st, 4);
        e.kind = sqlite3_column_int(st, 5);
        cb(&e, user);
    }
    sqlite3_finalize(st);
    return 0;
}

int db_totals_by_category(sqlite3 *db, const char *ym, int kind, CategoryCb cb, void *user)
{
    sqlite3_stmt *st;
    const char *sql =
        "SELECT category, SUM(amount_cents) AS total FROM entries "
        "WHERE substr(date, 1, 7) = ? AND kind = ? "
        "GROUP BY category ORDER BY total DESC;";

    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK)
        return 1;

    sqlite3_bind_text(st, 1, ym, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 2, kind);

    while (sqlite3_step(st) == SQLITE_ROW) {
        char cat[48];
        copy_text(cat, sizeof cat, sqlite3_column_text(st, 0));
        cb(cat, sqlite3_column_int64(st, 1), user);
    }
    sqlite3_finalize(st);
    return 0;
}

int db_month_total(sqlite3 *db, const char *ym, int kind, int64_t *out)
{
    sqlite3_stmt *st;
    const char *sql =
        "SELECT COALESCE(SUM(amount_cents), 0) FROM entries "
        "WHERE substr(date, 1, 7) = ? AND kind = ?;";

    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK)
        return 1;

    sqlite3_bind_text(st, 1, ym, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 2, kind);

    *out = 0;
    if (sqlite3_step(st) == SQLITE_ROW)
        *out = sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);
    return 0;
}
