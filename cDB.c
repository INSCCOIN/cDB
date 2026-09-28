/*
 * cDB — SQLite browser for /home/working/cScan/cscan.db
 * 480x320-first curses UI (cScan / cGotchi shape)
 *
 *   gcc -O2 -Wall -Wextra -o cdb cDB.c -lncurses -lsqlite3
 *   ./cdb
 *   ./cdb /path/to/other.db
 */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <curses.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEFAULT_DB "/home/working/cScan/cscan.db"
#define MAX_TAB    32
#define MAX_COL    24
#define MAX_ROW    512
#define MAX_CELL   64
#define FILTER_MAX 40

enum { PAGE_TABLES = 0, PAGE_ROWS = 1, PAGE_INFO = 2 };

typedef struct {
    char name[64];
    char type[16];
    int nrows;
} Tab;

typedef struct {
    char col[MAX_COL][32];
    int ncol;
} Schema;

typedef struct {
    char cell[MAX_COL][MAX_CELL];
} Row;

static sqlite3 *db;
static char dbpath[256];
static Tab tabs[MAX_TAB];
static int ntab, tsel, ttop;
static Schema sch;
static Row rows[MAX_ROW];
static int nrow, rsel, rtop;
static int page = PAGE_TABLES;
static char filter[FILTER_MAX];
static char status[96];
static int dirty = 1;

static void copy_str(char *d, size_t n, const char *s)
{
    size_t i = 0;
    if (!d || n == 0) return;
    if (!s) { d[0] = 0; return; }
    while (s[i] && i + 1 < n) { d[i] = s[i]; i++; }
    d[i] = 0;
}

static void put(int y, int x, const char *s, int n, int attr)
{
    int h, w, room;
    getmaxyx(stdscr, h, w);
    if (y < 0 || x < 0 || y >= h || x >= w || n <= 0) return;
    room = w - x;
    if (n > room) n = room;
    if (n <= 0) return;
    attron(attr);
    mvaddnstr(y, x, s, n);
    attroff(attr);
}

static int db_open(const char *path)
{
    copy_str(dbpath, sizeof dbpath, path);
    if (access(path, R_OK) != 0)
        return -1;
    if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK)
        return -1;
    return 0;
}

static int count_table(const char *name)
{
    sqlite3_stmt *st;
    char sql[160];
    int n = 0;
    snprintf(sql, sizeof sql, "SELECT COUNT(*) FROM \"%s\"", name);
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK)
        return 0;
    if (sqlite3_step(st) == SQLITE_ROW)
        n = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return n;
}

static void load_tables(void)
{
    sqlite3_stmt *st;
    ntab = 0;
    if (sqlite3_prepare_v2(db,
            "SELECT name, type FROM sqlite_master "
            "WHERE type IN ('table','view') AND name NOT LIKE 'sqlite_%' "
            "ORDER BY name",
            -1, &st, NULL) != SQLITE_OK)
        return;
    while (sqlite3_step(st) == SQLITE_ROW && ntab < MAX_TAB) {
        copy_str(tabs[ntab].name, sizeof tabs[ntab].name,
                 (const char *)sqlite3_column_text(st, 0));
        copy_str(tabs[ntab].type, sizeof tabs[ntab].type,
                 (const char *)sqlite3_column_text(st, 1));
        tabs[ntab].nrows = count_table(tabs[ntab].name);
        ntab++;
    }
    sqlite3_finalize(st);
    if (tsel >= ntab) tsel = ntab ? ntab - 1 : 0;
}

static void load_schema(const char *name)
{
    sqlite3_stmt *st;
    char sql[160];
    sch.ncol = 0;
    snprintf(sql, sizeof sql, "PRAGMA table_info(\"%s\")", name);
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK)
        return;
    while (sqlite3_step(st) == SQLITE_ROW && sch.ncol < MAX_COL) {
        copy_str(sch.col[sch.ncol], sizeof sch.col[0],
                 (const char *)sqlite3_column_text(st, 1));
        sch.ncol++;
    }
    sqlite3_finalize(st);
}

static int ci_has(const char *hay, const char *needle)
{
    size_t n, h, i, j;
    if (!needle || !needle[0]) return 1;
    if (!hay) return 0;
    n = strlen(needle);
    h = strlen(hay);
    if (n > h) return 0;
    for (i = 0; i + n <= h; i++) {
        for (j = 0; j < n; j++) {
            char a = hay[i + j], b = needle[j];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) break;
        }
        if (j == n) return 1;
    }
    return 0;
}

static int row_match(const Row *r)
{
    int i;
    if (!filter[0]) return 1;
    for (i = 0; i < sch.ncol; i++)
        if (ci_has(r->cell[i], filter)) return 1;
    return 0;
}

static void load_rows(const char *name)
{
    sqlite3_stmt *st;
    char sql[192];
    int i, keep;
    nrow = 0;
    snprintf(sql, sizeof sql, "SELECT * FROM \"%s\" LIMIT %d", name, MAX_ROW);
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) {
        snprintf(status, sizeof status, "query fail");
        return;
    }
    if (sch.ncol == 0)
        sch.ncol = sqlite3_column_count(st) < MAX_COL ? sqlite3_column_count(st) : MAX_COL;
    while (sqlite3_step(st) == SQLITE_ROW && nrow < MAX_ROW) {
        Row tmp;
        memset(&tmp, 0, sizeof tmp);
        for (i = 0; i < sch.ncol; i++) {
            const char *v = (const char *)sqlite3_column_text(st, i);
            copy_str(tmp.cell[i], sizeof tmp.cell[0], v ? v : "");
        }
        keep = row_match(&tmp);
        if (keep)
            rows[nrow++] = tmp;
    }
    sqlite3_finalize(st);
    if (rsel >= nrow) rsel = nrow ? nrow - 1 : 0;
    rtop = 0;
    snprintf(status, sizeof status, "%s  %d shown", name, nrow);
}

static void open_table(int idx)
{
    if (idx < 0 || idx >= ntab) return;
    load_schema(tabs[idx].name);
    load_rows(tabs[idx].name);
    page = PAGE_ROWS;
    rsel = rtop = 0;
    dirty = 1;
}

static void keep_visible(int *sel, int *off, int n, int vis)
{
    if (vis < 1) vis = 1;
    if (*sel < 0) *sel = 0;
    if (*sel >= n && n) *sel = n - 1;
    if (*sel < *off) *off = *sel;
    if (*sel >= *off + vis) *off = *sel - vis + 1;
    if (*off < 0) *off = 0;
}

static void show_help(void)
{
    static const char *lines[] = {
        "h        help",
        "enter    open table / row info",
        "esc/b    back",
        "/        filter rows",
        "r        reload",
        "up/down  move",
        "q        quit",
        "",
        "tables  networks  sightings  scans",
        NULL
    };
    int h, w, i;
    getmaxyx(stdscr, h, w);
    erase();
    put(0, 1, "HELP", w - 2, COLOR_PAIR(2));
    for (i = 0; lines[i] && i + 2 < h; i++)
        put(i + 2, 2, lines[i], w - 4, COLOR_PAIR(1));
    refresh();
    nodelay(stdscr, FALSE);
    getch();
    nodelay(stdscr, TRUE);
    dirty = 1;
}

static void show_row(void)
{
    int h, w, i, y = 2;
    char buf[96];
    if (!nrow || rsel < 0 || rsel >= nrow) return;
    getmaxyx(stdscr, h, w);
    erase();
    put(0, 1, "INFO", w - 2, COLOR_PAIR(2));
    put(1, 1, tabs[tsel].name, w - 2, COLOR_PAIR(1));
    for (i = 0; i < sch.ncol && y < h - 1; i++) {
        snprintf(buf, sizeof buf, "%-12.12s %s", sch.col[i], rows[rsel].cell[i]);
        put(y++, 2, buf, w - 4, COLOR_PAIR(1));
    }
    if (y < h)
        put(h - 1, 2, "any key", w - 4, COLOR_PAIR(1));
    refresh();
    nodelay(stdscr, FALSE);
    getch();
    nodelay(stdscr, TRUE);
    dirty = 1;
}

static void prompt_filter(void)
{
    int h, w;
    getmaxyx(stdscr, h, w);
    echo();
    curs_set(1);
    nodelay(stdscr, FALSE);
    move(h / 2, 1);
    clrtoeol();
    attron(COLOR_PAIR(2));
    mvaddnstr(h / 2, 1, "filter ", 7);
    attroff(COLOR_PAIR(2));
    filter[0] = 0;
    mvgetnstr(h / 2, 8, filter, FILTER_MAX - 1);
    noecho();
    curs_set(0);
    nodelay(stdscr, TRUE);
    if (page == PAGE_ROWS && ntab)
        load_rows(tabs[tsel].name);
    dirty = 1;
}

static void draw_tables(void)
{
    int h, w, list0 = 3, list_h, i, y;
    char buf[80];
    getmaxyx(stdscr, h, w);
    list_h = h - list0;
    keep_visible(&tsel, &ttop, ntab, list_h);
    erase();
    put(0, 1, "cDB  tables", w - 2, COLOR_PAIR(2));
    snprintf(buf, sizeof buf, "%s", dbpath);
    put(1, 1, buf, w - 2, COLOR_PAIR(1));
    snprintf(buf, sizeof buf, "%d tables", ntab);
    put(2, 1, buf, w - 2, COLOR_PAIR(1));
    y = list0;
    for (i = ttop; i < ntab && y < h; i++) {
        int attr = i == tsel ? COLOR_PAIR(2) : COLOR_PAIR(1);
        snprintf(buf, sizeof buf, " %-16.16s %5d  %s",
                 tabs[i].name, tabs[i].nrows, tabs[i].type);
        put(y++, 0, buf, w - 1, attr);
    }
}

static void draw_rows(void)
{
    int h, w, mid, list0 = 4, list_h, i, y, c0;
    char buf[96];
    getmaxyx(stdscr, h, w);
    mid = w / 2;
    if (mid < 16) mid = 16;
    list_h = h - list0;
    keep_visible(&rsel, &rtop, nrow, list_h);
    erase();
    snprintf(buf, sizeof buf, "cDB  %s", tabs[tsel].name);
    put(0, 1, buf, w - 2, COLOR_PAIR(2));
    snprintf(buf, sizeof buf, "%d rows%s%s", nrow, filter[0] ? "  /" : "", filter);
    put(1, 1, buf, w - 2, COLOR_PAIR(1));
    put(2, 1, status, w - 2, COLOR_PAIR(1));

    /* header: first two useful columns */
    c0 = 0;
    if (sch.ncol > 1) {
        /* prefer ssid + bssid if present */
        int a = 0, b = sch.ncol > 1 ? 1 : 0;
        for (i = 0; i < sch.ncol; i++) {
            if (!strcmp(sch.col[i], "ssid")) a = i;
            if (!strcmp(sch.col[i], "bssid")) b = i;
            if (!strcmp(sch.col[i], "ts") && a == 0) a = i;
        }
        snprintf(buf, sizeof buf, "%-12.12s  %s", sch.col[a], sch.col[b]);
        c0 = a;
        put(3, 1, buf, mid - 2, COLOR_PAIR(2));
        put(3, mid + 1, "cols", w - mid - 2, COLOR_PAIR(2));
        y = list0;
        for (i = rtop; i < nrow && y < h; i++) {
            int attr = i == rsel ? COLOR_PAIR(2) : COLOR_PAIR(1);
            snprintf(buf, sizeof buf, "%-12.12s  %-18.18s",
                     rows[i].cell[a], rows[i].cell[b]);
            put(y++, 0, buf, mid - 1, attr);
        }
        y = list0;
        if (nrow && rsel >= 0 && rsel < nrow) {
            for (i = 0; i < sch.ncol && y < h; i++) {
                snprintf(buf, sizeof buf, "%s", sch.col[i]);
                put(y++, mid + 1, buf, w - mid - 2, COLOR_PAIR(1));
            }
        }
        for (y = list0; y < h; y++)
            mvaddch(y, mid, ACS_VLINE | COLOR_PAIR(1));
        (void)c0;
        return;
    }
    put(3, 1, sch.ncol ? sch.col[0] : "row", w - 2, COLOR_PAIR(2));
    y = list0;
    for (i = rtop; i < nrow && y < h; i++) {
        int attr = i == rsel ? COLOR_PAIR(2) : COLOR_PAIR(1);
        put(y++, 1, rows[i].cell[0], w - 2, attr);
    }
}

static void draw(void)
{
    if (!dirty) return;
    if (page == PAGE_ROWS) draw_rows();
    else draw_tables();
    refresh();
    dirty = 0;
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : DEFAULT_DB;
    if (db_open(path) != 0) {
        fprintf(stderr, "cDB: cannot open %s\n", path);
        return 1;
    }
    load_tables();
    snprintf(status, sizeof status, "ready");

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    curs_set(0);
    start_color();
    use_default_colors();
    init_pair(1, COLOR_GREEN, -1);
    init_pair(2, COLOR_BLACK, COLOR_GREEN);
    init_pair(3, COLOR_YELLOW, -1);

    for (;;) {
        int k = getch();
        if (k == 'q' || k == 'Q') break;
        if (k == 'h' || k == 'H') {
            show_help();
        } else if (k == 'r' || k == 'R') {
            if (page == PAGE_TABLES) load_tables();
            else if (ntab) load_rows(tabs[tsel].name);
            dirty = 1;
        } else if (k == '/') {
            prompt_filter();
        } else if (k == 27 || k == 'b' || k == 'B') {
            if (page == PAGE_ROWS) {
                page = PAGE_TABLES;
                filter[0] = 0;
                dirty = 1;
            }
        } else if (k == KEY_UP) {
            if (page == PAGE_TABLES && tsel > 0) tsel--;
            if (page == PAGE_ROWS && rsel > 0) rsel--;
            dirty = 1;
        } else if (k == KEY_DOWN) {
            if (page == PAGE_TABLES && tsel + 1 < ntab) tsel++;
            if (page == PAGE_ROWS && rsel + 1 < nrow) rsel++;
            dirty = 1;
        } else if (k == '\n' || k == KEY_ENTER) {
            if (page == PAGE_TABLES) open_table(tsel);
            else show_row();
        } else if (k == KEY_RESIZE)
            dirty = 1;
        draw();
        napms(40);
    }
    endwin();
    sqlite3_close(db);
    return 0;
}
