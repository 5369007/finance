/* Controle Financeiro - interface gráfica (Raylib).
 * Usa a mesma camada de dados (db.c) e o mesmo arquivo financas.db
 * da versão de terminal. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <stdint.h>
#include "raylib.h"
#include "db.h"

#define WIN_W 1100
#define WIN_H 700

#define C_BG     ((Color){243, 245, 249, 255})
#define C_PANEL  ((Color){255, 255, 255, 255})
#define C_TEXT   ((Color){30, 35, 45, 255})
#define C_MUTED  ((Color){120, 128, 142, 255})
#define C_BORDER ((Color){205, 210, 220, 255})
#define C_BLUE   ((Color){52, 100, 200, 255})
#define C_GREEN  ((Color){30, 140, 85, 255})
#define C_RED    ((Color){200, 60, 60, 255})
#define C_GRAY   ((Color){150, 158, 172, 255})
#define C_WHITE  ((Color){255, 255, 255, 255})

/* ---------- fonte e texto ---------- */

static Font g_font;

static void load_font(void)
{
    static const char *paths[] = { "C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf" };
    int cps[224];
    for (int i = 0; i < 224; i++) cps[i] = 32 + i;   /* Latin-1: acentos do português */

    for (int p = 0; p < 2; p++) {
        if (!FileExists(paths[p])) continue;
        Font f = LoadFontEx(paths[p], 32, cps, 224);
        if (f.texture.id != 0) {
            g_font = f;
            SetTextureFilter(g_font.texture, TEXTURE_FILTER_BILINEAR);
            return;
        }
    }
    g_font = GetFontDefault();
}

static float text_w(const char *t, float size)
{
    return MeasureTextEx(g_font, t, size, 0.5f).x;
}

static void draw_text(const char *t, float x, float y, float size, Color c)
{
    DrawTextEx(g_font, t, (Vector2){ x, y }, size, 0.5f, c);
}

static void draw_text_right(const char *t, float right, float y, float size, Color c)
{
    draw_text(t, right - text_w(t, size), y, size, c);
}

/* Corta o texto com "..." para caber em maxw pixels. */
static void fit_text(const char *src, float size, float maxw, char *out, size_t outsz)
{
    char tmp[200];
    snprintf(out, outsz, "%s", src);
    if (text_w(out, size) <= maxw) return;

    size_t len = strlen(out);
    while (len > 0) {
        len--;
        while (len > 0 && ((unsigned char)out[len] & 0xC0) == 0x80) len--;
        out[len] = '\0';
        snprintf(tmp, sizeof tmp, "%s...", out);
        if (text_w(tmp, size) <= maxw) { snprintf(out, outsz, "%s", tmp); return; }
    }
}

/* ---------- dinheiro e datas ---------- */

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

/* 350000 -> "R$ 3.500,00"; negativos ganham "-" na frente. */
static void format_money(int64_t cents, char *out, size_t size)
{
    int neg = cents < 0;
    if (neg) cents = -cents;

    long long whole = cents / 100, frac = cents % 100;
    char digits[32], grouped[48];
    snprintf(digits, sizeof digits, "%lld", whole);

    int n = (int)strlen(digits), j = 0;
    for (int i = 0; i < n; i++) {
        if (i > 0 && (n - i) % 3 == 0) grouped[j++] = '.';
        grouped[j++] = digits[i];
    }
    grouped[j] = '\0';

    snprintf(out, size, "%sR$ %s,%02lld", neg ? "-" : "", grouped, frac);
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

static void trim(char *s)
{
    size_t len = strlen(s), start = 0;
    while (len > 0 && isspace((unsigned char)s[len - 1])) s[--len] = '\0';
    while (s[start] && isspace((unsigned char)s[start])) start++;
    if (start) memmove(s, s + start, strlen(s + start) + 1);
}

/* ---------- estado do programa ---------- */

typedef struct { char name[48]; int64_t total; } CatTotal;
typedef struct { char text[96]; size_t max; } Field;

enum { F_DATE, F_AMOUNT, F_CATEGORY, F_DESC, F_COUNT };

static sqlite3 *g_db;
static int g_year, g_month;

static Entry *g_entries;
static int g_n, g_cap;
static CatTotal g_cats[32];
static int g_ncats;
static int64_t g_income, g_expense;

static float g_scroll;
static int g_pending_delete = -1;
static int g_editing_id = 0;          /* 0 = criando; >0 = editando esse id */

static int g_kind = KIND_DESPESA;
static int g_focus = -1;
static Field g_fields[F_COUNT] = { { "", 10 }, { "", 15 }, { "", 47 }, { "", 95 } };
static const char *g_labels[F_COUNT] = {
    "Data (AAAA-MM-DD)", "Valor (ex: 45,90)", "Categoria (ex: Mercado)", "Descrição (opcional)"
};
static const char *g_hints[F_COUNT] = { "", "45,90", "Mercado", "Compras da semana" };
static const Rectangle g_field_rects[F_COUNT] = {
    { 40, 234, 300, 36 }, { 40, 306, 300, 36 }, { 40, 378, 300, 36 }, { 40, 450, 300, 36 }
};

static char g_msg[128];
static Color g_msg_color;

static const char *g_months[12] = {
    "Janeiro", "Fevereiro", "Março", "Abril", "Maio", "Junho",
    "Julho", "Agosto", "Setembro", "Outubro", "Novembro", "Dezembro"
};

static void set_msg(const char *text, Color c)
{
    snprintf(g_msg, sizeof g_msg, "%s", text);
    g_msg_color = c;
}

/* ---------- acesso aos dados ---------- */

static void on_entry(const Entry *e, void *user)
{
    (void)user;
    if (g_n == g_cap) {
        int ncap = g_cap ? g_cap * 2 : 64;
        Entry *p = realloc(g_entries, (size_t)ncap * sizeof *p);
        if (!p) return;
        g_entries = p;
        g_cap = ncap;
    }
    g_entries[g_n++] = *e;
}

static void on_category(const char *cat, int64_t total, void *user)
{
    (void)user;
    if (g_ncats >= 32) return;
    snprintf(g_cats[g_ncats].name, sizeof g_cats[g_ncats].name, "%s", cat);
    g_cats[g_ncats].total = total;
    g_ncats++;
}

static void reload(void)
{
    char ym[16];
    snprintf(ym, sizeof ym, "%04d-%02d", g_year, g_month);

    g_n = 0;
    g_ncats = 0;
    g_pending_delete = -1;
    g_income = g_expense = 0;

    db_list_month(g_db, ym, on_entry, NULL);
    db_totals_by_category(g_db, ym, KIND_DESPESA, on_category, NULL);
    db_month_total(g_db, ym, KIND_RECEITA, &g_income);
    db_month_total(g_db, ym, KIND_DESPESA, &g_expense);
}

static void change_month(int delta)
{
    g_month += delta;
    if (g_month < 1)  { g_month = 12; g_year--; }
    if (g_month > 12) { g_month = 1;  g_year++; }
    g_scroll = 0;
    reload();
}

static void reset_form(void)
{
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    snprintf(g_fields[F_DATE].text, sizeof g_fields[F_DATE].text, "%04d-%02d-%02d",
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday);
    g_fields[F_AMOUNT].text[0] = '\0';
    g_fields[F_CATEGORY].text[0] = '\0';
    g_fields[F_DESC].text[0] = '\0';
}

static void start_edit(const Entry *e)
{
    g_editing_id = e->id;
    g_kind = e->kind;
    g_pending_delete = -1;

    snprintf(g_fields[F_DATE].text, sizeof g_fields[F_DATE].text, "%s", e->date);
    snprintf(g_fields[F_AMOUNT].text, sizeof g_fields[F_AMOUNT].text, "%lld,%02lld",
             (long long)(e->amount_cents / 100), (long long)(e->amount_cents % 100));
    snprintf(g_fields[F_CATEGORY].text, sizeof g_fields[F_CATEGORY].text, "%s", e->category);
    snprintf(g_fields[F_DESC].text, sizeof g_fields[F_DESC].text, "%s",
             strcmp(e->description, "-") == 0 ? "" : e->description);

    g_focus = F_AMOUNT;
    set_msg("Editando lançamento. Esc cancela.", C_BLUE);
}

static void cancel_edit(void)
{
    g_editing_id = 0;
    reset_form();
    g_focus = -1;
    set_msg("Edição cancelada.", C_MUTED);
}

/* Lê e valida o formulário. Retorna 0 se está tudo certo. */
static int read_form(Entry *e)
{
    memset(e, 0, sizeof *e);
    e->kind = g_kind;

    trim(g_fields[F_DATE].text);
    trim(g_fields[F_AMOUNT].text);
    trim(g_fields[F_CATEGORY].text);
    trim(g_fields[F_DESC].text);

    if (!valid_date(g_fields[F_DATE].text)) {
        set_msg("Data inválida. Use AAAA-MM-DD (ex: 2026-11-20).", C_RED);
        return 1;
    }
    snprintf(e->date, sizeof e->date, "%.10s", g_fields[F_DATE].text);

    if (parse_money(g_fields[F_AMOUNT].text, &e->amount_cents) != 0) {
        set_msg("Valor inválido. Ex: 45,90 (maior que zero).", C_RED);
        return 1;
    }
    if (g_fields[F_CATEGORY].text[0] == '\0') {
        set_msg("Preencha a categoria.", C_RED);
        return 1;
    }
    snprintf(e->category, sizeof e->category, "%s", g_fields[F_CATEGORY].text);
    snprintf(e->description, sizeof e->description, "%s",
             g_fields[F_DESC].text[0] ? g_fields[F_DESC].text : "-");
    return 0;
}

static void save_entry(void)
{
    Entry e;
    if (read_form(&e) != 0) return;

    int old_year = g_year, old_month = g_month;

    if (g_editing_id > 0) {
        int updated = 0;
        e.id = g_editing_id;
        if (db_update(g_db, &e, &updated) != 0) {
            set_msg("Erro ao atualizar no banco de dados.", C_RED);
            return;
        }
        g_editing_id = 0;
        reset_form();
        g_focus = -1;
        set_msg(updated ? "Lançamento atualizado." : "Esse lançamento não existe mais.",
                updated ? C_GREEN : C_RED);
    } else {
        if (db_add(g_db, &e) != 0) {
            set_msg("Erro ao salvar no banco de dados.", C_RED);
            return;
        }
        g_fields[F_AMOUNT].text[0] = '\0';
        g_fields[F_DESC].text[0] = '\0';
        g_focus = F_AMOUNT;
        set_msg("Lançamento salvo.", C_GREEN);
    }

    /* mostra o mês do lançamento; só volta ao topo da lista se o mês mudou */
    sscanf(e.date, "%d-%d", &g_year, &g_month);
    if (g_year != old_year || g_month != old_month) g_scroll = 0;
    reload();
}

/* ---------- widgets ---------- */

static int button(Rectangle r, const char *label, Color base, float size, int enabled)
{
    Vector2 m = GetMousePosition();
    int hover = enabled && CheckCollisionPointRec(m, r);
    Color c = base;
    if (hover) c = (Color){ (unsigned char)(base.r * 8 / 10), (unsigned char)(base.g * 8 / 10),
                            (unsigned char)(base.b * 8 / 10), 255 };

    DrawRectangleRounded(r, 0.25f, 8, c);
    Vector2 sz = MeasureTextEx(g_font, label, size, 0.5f);
    draw_text(label, r.x + (r.width - sz.x) / 2, r.y + (r.height - sz.y) / 2, size, C_WHITE);

    return hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
}

static void handle_typing(Field *f)
{
    int cp;
    while ((cp = GetCharPressed()) > 0) {
        if (cp < 32 || cp == 127) continue;
        int sz = 0;
        const char *u = CodepointToUTF8(cp, &sz);
        size_t len = strlen(f->text);
        if (len + (size_t)sz <= f->max) {
            memcpy(f->text + len, u, (size_t)sz);
            f->text[len + sz] = '\0';
        }
    }
    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) {
        size_t len = strlen(f->text);
        if (len > 0) {
            len--;
            while (len > 0 && ((unsigned char)f->text[len] & 0xC0) == 0x80) len--;
            f->text[len] = '\0';
        }
    }
}

static void draw_field(int i)
{
    Rectangle r = g_field_rects[i];
    int focused = (g_focus == i);

    draw_text(g_labels[i], r.x, r.y - 22, 15, C_MUTED);
    DrawRectangleRec(r, C_PANEL);
    DrawRectangleLinesEx(r, focused ? 2 : 1, focused ? C_BLUE : C_BORDER);

    const char *p = g_fields[i].text;
    if (p[0] == '\0' && !focused) {
        draw_text(g_hints[i], r.x + 12, r.y + 8, 18, C_BORDER);
        return;
    }

    /* se o texto for maior que a caixa, mostra o final */
    while (*p && text_w(p, 18) > r.width - 26) {
        p++;
        while (((unsigned char)*p & 0xC0) == 0x80) p++;
    }
    draw_text(p, r.x + 12, r.y + 8, 18, C_TEXT);

    if (focused && ((int)(GetTime() * 2) % 2 == 0)) {
        float cx = r.x + 12 + text_w(p, 18) + 2;
        DrawRectangle((int)cx, (int)r.y + 8, 2, 20, C_TEXT);
    }
}

/* ---------- desenho das áreas ---------- */

static void draw_summary(float x, const char *label, int64_t cents, Color c)
{
    char buf[48];
    format_money(cents, buf, sizeof buf);
    draw_text(label, x, 16, 15, C_MUTED);
    draw_text(buf, x, 38, 26, c);
}

static void draw_header(void)
{
    DrawRectangle(0, 0, WIN_W, 90, C_PANEL);
    DrawRectangle(0, 90, WIN_W, 1, C_BORDER);

    draw_text("Controle Financeiro", 20, 10, 24, C_TEXT);

    if (button((Rectangle){ 20, 48, 32, 30 }, "<", C_BLUE, 18, 1)) change_month(-1);
    if (button((Rectangle){ 224, 48, 32, 30 }, ">", C_BLUE, 18, 1)) change_month(+1);

    char lbl[48];
    snprintf(lbl, sizeof lbl, "%s %d", g_months[g_month - 1], g_year);
    draw_text(lbl, 56 + (168 - text_w(lbl, 19)) / 2, 52, 19, C_TEXT);

    int64_t saldo = g_income - g_expense;
    draw_summary(520, "Receitas", g_income, C_GREEN);
    draw_summary(730, "Despesas", g_expense, C_RED);
    draw_summary(900, "Saldo", saldo, saldo < 0 ? C_RED : C_BLUE);
}

static void draw_form(void)
{
    DrawRectangle(20, 110, 340, 570, C_PANEL);
    if (g_editing_id > 0) draw_text("Editar lançamento", 40, 124, 20, C_BLUE);
    else                  draw_text("Novo lançamento", 40, 124, 20, C_TEXT);

    int desp = (g_kind == KIND_DESPESA);
    if (button((Rectangle){ 40, 160, 145, 36 }, "Despesa", desp ? C_RED : C_GRAY, 17, 1))
        g_kind = KIND_DESPESA;
    if (button((Rectangle){ 195, 160, 145, 36 }, "Receita", !desp ? C_GREEN : C_GRAY, 17, 1))
        g_kind = KIND_RECEITA;

    for (int i = 0; i < F_COUNT; i++) draw_field(i);

    if (g_editing_id > 0) {
        if (button((Rectangle){ 40, 510, 205, 42 }, "Salvar alterações", C_BLUE, 17, 1))
            save_entry();
        if (button((Rectangle){ 255, 510, 85, 42 }, "Cancelar", C_GRAY, 16, 1))
            cancel_edit();
    } else {
        if (button((Rectangle){ 40, 510, 300, 42 }, "Salvar", C_BLUE, 18, 1))
            save_entry();
    }

    if (g_msg[0]) {
        char buf[128];
        fit_text(g_msg, 15, 300, buf, sizeof buf);
        draw_text(buf, 40, 566, 15, g_msg_color);
    }
}

static void draw_list(void)
{
    const Rectangle view = { 380, 172, 700, 240 };
    const float row_h = 30;
    Vector2 m = GetMousePosition();
    int in_view = CheckCollisionPointRec(m, view);
    int delete_id = 0;

    DrawRectangle(380, 110, 700, 310, C_PANEL);
    draw_text("Lançamentos do mês", 400, 122, 20, C_TEXT);
    draw_text("Data", 400, 150, 14, C_MUTED);
    draw_text("Categoria", 490, 150, 14, C_MUTED);
    draw_text("Descrição", 630, 150, 14, C_MUTED);
    draw_text_right("Valor", 980, 150, 14, C_MUTED);

    float content_h = g_n * row_h;
    float max_scroll = content_h > view.height ? content_h - view.height : 0;
    if (in_view) g_scroll -= GetMouseWheelMove() * row_h;
    if (g_scroll < 0) g_scroll = 0;
    if (g_scroll > max_scroll) g_scroll = max_scroll;

    if (g_n == 0)
        draw_text("Nenhum lançamento neste mês.", 400, 190, 17, C_MUTED);

    BeginScissorMode((int)view.x, (int)view.y, (int)view.width, (int)view.height);
    for (int i = 0; i < g_n; i++) {
        float y = view.y + i * row_h - g_scroll;
        if (y + row_h < view.y || y > view.y + view.height) continue;

        const Entry *e = &g_entries[i];
        if (e->id == g_pending_delete) {
            DrawRectangle(390, (int)y, 680, (int)row_h - 2, (Color){ 252, 232, 232, 255 });
            draw_text("Apagar este lançamento?", 400, y + 5, 16, C_RED);
            if (button((Rectangle){ 900, y + 3, 60, 24 }, "Sim", C_RED, 15, in_view)) delete_id = e->id;
            if (button((Rectangle){ 968, y + 3, 60, 24 }, "Não", C_GRAY, 15, in_view)) g_pending_delete = -1;
            continue;
        }

        if (e->id == g_editing_id)
            DrawRectangle(390, (int)y, 680, (int)row_h - 2, (Color){ 225, 235, 252, 255 });
        else if (i % 2 == 1)
            DrawRectangle(390, (int)y, 680, (int)row_h - 2, (Color){ 248, 249, 252, 255 });

        char cat[64], desc[128], money[48];
        fit_text(e->category, 16, 125, cat, sizeof cat);
        fit_text(e->description, 16, 215, desc, sizeof desc);
        int is_income = (e->kind == KIND_RECEITA);
        format_money(is_income ? e->amount_cents : -e->amount_cents, money, sizeof money);

        draw_text(e->date, 400, y + 5, 16, C_MUTED);
        draw_text(cat, 490, y + 5, 16, C_TEXT);
        draw_text(desc, 630, y + 5, 16, C_MUTED);
        draw_text_right(money, 980, y + 5, 16, is_income ? C_GREEN : C_RED);

        if (button((Rectangle){ 988, y + 4, 46, 22 }, "Editar", C_BLUE, 13, in_view))
            start_edit(e);
        if (button((Rectangle){ 1038, y + 4, 24, 22 }, "x", C_GRAY, 14, in_view))
            g_pending_delete = e->id;
    }
    EndScissorMode();

    if (max_scroll > 0) {
        float bar_h = view.height * view.height / content_h;
        float bar_y = view.y + (g_scroll / max_scroll) * (view.height - bar_h);
        DrawRectangle(1070, (int)bar_y, 5, (int)bar_h, C_BORDER);
    }

    if (delete_id > 0) {
        int removed = 0;
        if (delete_id == g_editing_id) cancel_edit();
        if (db_delete(g_db, delete_id, &removed) == 0) set_msg("Lançamento apagado.", C_MUTED);
        reload();
    }
}

static void draw_chart(void)
{
    static const Color palette[6] = {
        { 52, 100, 200, 255 }, { 30, 140, 85, 255 }, { 220, 140, 40, 255 },
        { 150, 80, 190, 255 }, { 200, 60, 60, 255 }, { 40, 160, 180, 255 }
    };

    DrawRectangle(380, 430, 700, 250, C_PANEL);
    draw_text("Despesas por categoria", 400, 442, 20, C_TEXT);

    if (g_ncats == 0) {
        draw_text("Sem despesas neste mês.", 400, 485, 17, C_MUTED);
        return;
    }

    int64_t max = g_cats[0].total;           /* já vem ordenado do maior para o menor */
    int shown = g_ncats < 6 ? g_ncats : 6;

    for (int i = 0; i < shown; i++) {
        float y = 482 + i * 32;
        char name[64], money[48];
        fit_text(g_cats[i].name, 16, 140, name, sizeof name);
        format_money(g_cats[i].total, money, sizeof money);

        float len = max > 0 ? (float)((double)g_cats[i].total / (double)max * 370.0) : 0;
        if (len < 4) len = 4;

        draw_text(name, 400, y + 3, 16, C_TEXT);
        DrawRectangle(550, (int)y, (int)len, 22, palette[i]);
        draw_text(money, 550 + len + 8, y + 3, 16, C_MUTED);
    }
}

/* ---------- programa principal ---------- */

int main(void)
{
    time_t now = time(NULL);
    struct tm *lt = localtime(&now);
    g_year = lt->tm_year + 1900;
    g_month = lt->tm_mon + 1;
    reset_form();

    if (db_open(&g_db, "financas.db") != 0) {
        fprintf(stderr, "Nao foi possivel abrir o banco de dados.\n");
        return 1;
    }

    InitWindow(WIN_W, WIN_H, "Controle Financeiro");
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);        /* ESC não fecha o programa por acidente */
    load_font();
    reload();

    while (!WindowShouldClose()) {
        /* clique define qual campo está ativo */
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            Vector2 m = GetMousePosition();
            int hit = -1;
            for (int i = 0; i < F_COUNT; i++)
                if (CheckCollisionPointRec(m, g_field_rects[i])) hit = i;
            if (hit >= 0 || CheckCollisionPointRec(m, (Rectangle){ 20, 110, 340, 570 }))
                g_focus = hit;
        }
        if (IsKeyPressed(KEY_TAB)) g_focus = (g_focus + 1) % F_COUNT;
        if (IsKeyPressed(KEY_ESCAPE) && g_editing_id > 0) cancel_edit();

        if (g_focus >= 0) {
            handle_typing(&g_fields[g_focus]);
            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) save_entry();
        } else {
            while (GetCharPressed() > 0) { }   /* descarta teclas digitadas sem campo ativo */
        }

        BeginDrawing();
        ClearBackground(C_BG);
        draw_header();
        draw_form();
        draw_list();
        draw_chart();
        EndDrawing();
    }

    if (g_font.texture.id != GetFontDefault().texture.id) UnloadFont(g_font);
    CloseWindow();
    sqlite3_close(g_db);
    free(g_entries);
    return 0;
}
