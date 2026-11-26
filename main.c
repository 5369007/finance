/* Controle financeiro - versão 1 (terminal).
 * A camada de dados (db.c) é separada da tela, para depois
 * trocarmos este main.c por uma interface gráfica com Raylib. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include "db.h"

#ifdef _WIN32
#include <windows.h>
#endif

/* ---------- entrada e formatação ---------- */

static void read_line(const char *prompt, char *buf, size_t size)
{
    printf("%s", prompt);
    fflush(stdout);
    if (!fgets(buf, (int)size, stdin)) { buf[0] = '\0'; return; }
    buf[strcspn(buf, "\r\n")] = '\0';
}

/* "12,50" ou "12.50" ou "12" -> centavos. Retorna 0 se válido. */
static int parse_money(const char *s, int64_t *cents)
{
    int64_t whole = 0, frac = 0;
    int digits = 0, frac_digits = 0, seen_sep = 0;

    for (; *s; s++) {
        if (isdigit((unsigned char)*s)) {
            digits++;
            if (seen_sep) {
                if (++frac_digits > 2) return 1;
                frac = frac * 10 + (*s - '0');
            } else {
                whole = whole * 10 + (*s - '0');
                if (whole > 1000000000LL) return 1;
            }
        } else if ((*s == ',' || *s == '.') && !seen_sep) {
            seen_sep = 1;
        } else {
            return 1;
        }
    }
    if (digits == 0) return 1;
    if (frac_digits == 1) frac *= 10;

    *cents = whole * 100 + frac;
    return *cents > 0 ? 0 : 1;
}

static void format_money(int64_t cents, char *out, size_t size)
{
    snprintf(out, size, "R$ %lld,%02lld", (long long)(cents / 100), (long long)(cents % 100));
}

static int valid_date(const char *s)
{
    int y, m, d;
    char extra;
    if (strlen(s) != 10 || sscanf(s, "%4d-%2d-%2d%c", &y, &m, &d, &extra) != 3) return 0;
    if (s[4] != '-' || s[7] != '-') return 0;
    if (y < 1900 || y > 2200 || m < 1 || m > 12 || d < 1) return 0;

    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int max = days[m - 1];
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) max = 29;
    return d <= max;
}

static int valid_month(const char *s)
{
    char date[11];
    if (strlen(s) != 7) return 0;
    snprintf(date, sizeof date, "%s-01", s);
    return valid_date(date);
}

static void current_ym(char *out, size_t size)
{
    time_t t = time(NULL);
    strftime(out, size, "%Y-%m", localtime(&t));
}

static void current_date(char *out, size_t size)
{
    time_t t = time(NULL);
    strftime(out, size, "%Y-%m-%d", localtime(&t));
}

static void ask_month(char *ym, size_t size)
{
    char def[8], in[32];
    current_ym(def, sizeof def);
    for (;;) {
        printf("Mês (AAAA-MM) [Enter = %s]: ", def);
        fflush(stdout);
        if (!fgets(in, sizeof in, stdin)) { snprintf(ym, size, "%s", def); return; }
        in[strcspn(in, "\r\n")] = '\0';
        if (in[0] == '\0') { snprintf(ym, size, "%s", def); return; }
        if (valid_month(in)) { snprintf(ym, size, "%s", in); return; }
        puts("Mês inválido. Exemplo: 2026-11");
    }
}

/* ---------- callbacks de exibição ---------- */

static void print_entry(const Entry *e, void *user)
{
    (void)user;
    char money[32];
    format_money(e->amount_cents, money, sizeof money);
    printf("%4d  %s  %-8s  %-14.14s  %-24.24s  %14s\n",
           e->id, e->date, e->kind == KIND_RECEITA ? "receita" : "despesa",
           e->category, e->description, money);
}

static void print_category(const char *cat, int64_t total, void *user)
{
    (void)user;
    char money[32];
    format_money(total, money, sizeof money);
    printf("  %-20.20s %14s\n", cat, money);
}

/* ---------- ações do menu ---------- */

static void action_add(sqlite3 *db)
{
    Entry e;
    char buf[128];
    memset(&e, 0, sizeof e);

    for (;;) {
        read_line("Tipo (d = despesa, r = receita): ", buf, sizeof buf);
        if (tolower((unsigned char)buf[0]) == 'd') { e.kind = KIND_DESPESA; break; }
        if (tolower((unsigned char)buf[0]) == 'r') { e.kind = KIND_RECEITA; break; }
        puts("Digite d ou r.");
    }

    char today[11];
    current_date(today, sizeof today);
    for (;;) {
        printf("Data (AAAA-MM-DD) [Enter = %s]: ", today);
        fflush(stdout);
        if (!fgets(buf, sizeof buf, stdin)) return;
        buf[strcspn(buf, "\r\n")] = '\0';
        if (buf[0] == '\0') { snprintf(e.date, sizeof e.date, "%s", today); break; }
        if (valid_date(buf)) { snprintf(e.date, sizeof e.date, "%.10s", buf); break; }
        puts("Data inválida. Exemplo: 2026-11-20");
    }

    for (;;) {
        read_line("Valor (ex: 45,90): ", buf, sizeof buf);
        if (parse_money(buf, &e.amount_cents) == 0) break;
        puts("Valor inválido. Use até 2 casas decimais, maior que zero.");
    }

    for (;;) {
        read_line("Categoria (ex: Mercado): ", e.category, sizeof e.category);
        if (e.category[0]) break;
        puts("A categoria não pode ficar vazia.");
    }

    read_line("Descrição (opcional): ", e.description, sizeof e.description);
    if (e.description[0] == '\0') snprintf(e.description, sizeof e.description, "-");

    puts(db_add(db, &e) == 0 ? "Lançamento salvo." : "Erro ao salvar.");
}

static void action_list(sqlite3 *db)
{
    char ym[8];
    ask_month(ym, sizeof ym);
    printf("\n  ID  Data        Tipo      Categoria       Descrição                          Valor\n");
    db_list_month(db, ym, print_entry, NULL);
}

static void action_summary(sqlite3 *db)
{
    char ym[8], money[32];
    int64_t rec = 0, desp = 0;
    ask_month(ym, sizeof ym);

    db_month_total(db, ym, KIND_RECEITA, &rec);
    db_month_total(db, ym, KIND_DESPESA, &desp);

    printf("\nResumo de %s\n", ym);
    puts("Despesas por categoria:");
    db_totals_by_category(db, ym, KIND_DESPESA, print_category, NULL);

    format_money(rec, money, sizeof money);
    printf("Receitas: %s\n", money);
    format_money(desp, money, sizeof money);
    printf("Despesas: %s\n", money);

    int64_t saldo = rec - desp;
    int64_t abs_saldo = saldo < 0 ? -saldo : saldo;
    format_money(abs_saldo, money, sizeof money);
    printf("Saldo:    %s%s\n", saldo < 0 ? "-" : "", money);
}

static void action_delete(sqlite3 *db)
{
    char buf[32];
    read_line("ID do lançamento a apagar: ", buf, sizeof buf);
    char *end;
    long id = strtol(buf, &end, 10);
    if (end == buf || *end != '\0' || id <= 0) { puts("ID inválido."); return; }

    int removed = 0;
    if (db_delete(db, (int)id, &removed) != 0) puts("Erro ao apagar.");
    else puts(removed ? "Lançamento apagado." : "Não existe lançamento com esse ID.");
}

int main(void)
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    sqlite3 *db;
    if (db_open(&db, "financas.db") != 0) {
        fprintf(stderr, "Não foi possível abrir o banco de dados.\n");
        return 1;
    }

    for (;;) {
        char op[16];
        puts("\n=== Controle Financeiro ===");
        puts("1) Novo lançamento");
        puts("2) Listar lançamentos do mês");
        puts("3) Resumo do mês");
        puts("4) Apagar lançamento");
        puts("0) Sair");
        read_line("> ", op, sizeof op);

        if (op[0] == '0' || (op[0] == '\0' && feof(stdin))) break;
        switch (op[0]) {
            case '1': action_add(db); break;
            case '2': action_list(db); break;
            case '3': action_summary(db); break;
            case '4': action_delete(db); break;
            default:  puts("Opção inválida.");
        }
    }

    sqlite3_close(db);
    return 0;
}
