#ifndef DB_H
#define DB_H

#include <stdint.h>
#include "sqlite3.h"

#define KIND_DESPESA 0
#define KIND_RECEITA 1

typedef struct {
    int     id;
    char    date[11];          /* AAAA-MM-DD */
    char    category[48];
    char    description[96];
    int64_t amount_cents;      /* valor em centavos, sempre positivo */
    int     kind;              /* KIND_DESPESA ou KIND_RECEITA */
} Entry;

typedef void (*EntryCb)(const Entry *e, void *user);
typedef void (*CategoryCb)(const char *category, int64_t total_cents, void *user);

/* Todas retornam 0 em caso de sucesso, diferente de 0 em erro. */
int db_open(sqlite3 **db, const char *path);
int db_add(sqlite3 *db, const Entry *e);
int db_update(sqlite3 *db, const Entry *e, int *updated);   /* atualiza o lançamento e->id */
int db_delete(sqlite3 *db, int id, int *removed);
int db_list_month(sqlite3 *db, const char *ym, EntryCb cb, void *user);
int db_totals_by_category(sqlite3 *db, const char *ym, int kind, CategoryCb cb, void *user);
int db_month_total(sqlite3 *db, const char *ym, int kind, int64_t *out);

#endif
