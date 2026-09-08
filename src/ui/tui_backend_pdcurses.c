#include "tui.h"
#include <curses.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static volatile sig_atomic_t interrupted;
static int dim_pairs;
static void on_signal(int sig) {
    (void)sig;
    interrupted = 1;
}
static void colors(const MapDb *db) {
    start_color();
    use_default_colors();
    init_pair(1, COLOR_CYAN, -1);
    dim_pairs = can_change_color() && COLORS >= 288 && COLOR_PAIRS >= 260;
    for (size_t i = 0; i < db->metro.lines.rows.size; i++) {
        int id = db->metro.lines.rows.items[i].id;
        unsigned rgb = map_display_color(db->colors[id]);
        int color = 32 + id;
        if (can_change_color() && color < COLORS)
            init_color((short)color, (short)(((rgb >> 16) & 255) * 1000 / 255),
                       (short)(((rgb >> 8) & 255) * 1000 / 255), (short)((rgb & 255) * 1000 / 255));
        else
            color = 1 + id % 7;
        init_pair((short)(4 + id), (short)color, -1);
        if (dim_pairs) {
            int muted = 160 + id;
            init_color((short)muted, (short)(((rgb >> 16) & 255) * 500 / 255),
                       (short)(((rgb >> 8) & 255) * 500 / 255), (short)((rgb & 255) * 500 / 255));
            init_pair((short)(132 + id), (short)muted, -1);
        }
    }
}
static void present(MapFrame *f) {
    erase();
    for (int y = 0; y < f->height; y++)
        for (int x = 0; x < f->width; x++) {
            MapCell c = f->cells[y * f->width + x];
            if (c.continuation)
                continue;
            wchar_t str[3] = {(wchar_t)(c.glyph ? c.glyph : ' '), 0, 0};
            cchar_t value;
            if (c.glyph > 0xffff) {
                str[0] = (wchar_t)(0xd800 + ((c.glyph - 0x10000) >> 10));
                str[1] = (wchar_t)(0xdc00 + (c.glyph & 1023));
            }
            short pair = c.color;
            if (c.dim && c.color >= 5 && dim_pairs)
                pair += 128;
            setcchar(&value, str, c.dim ? A_DIM : A_NORMAL, pair, NULL);
            mvadd_wch(y, x, &value);
        }
    wnoutrefresh(stdscr);
    doupdate();
}
static void append_utf8(char *s, size_t cap, unsigned c) {
    char b[5];
    int n = 0;
    if (c < 32 || c == 127 || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff))
        return;
    if (c < 0x80)
        b[n++] = (char)c;
    else if (c < 0x800) {
        b[n++] = (char)(0xc0 | (c >> 6));
        b[n++] = (char)(0x80 | (c & 63));
    } else if (c < 0x10000) {
        b[n++] = (char)(0xe0 | (c >> 12));
        b[n++] = (char)(0x80 | ((c >> 6) & 63));
        b[n++] = (char)(0x80 | (c & 63));
    } else {
        b[n++] = (char)(0xf0 | (c >> 18));
        b[n++] = (char)(0x80 | ((c >> 12) & 63));
        b[n++] = (char)(0x80 | ((c >> 6) & 63));
        b[n++] = (char)(0x80 | (c & 63));
    }
    size_t len = strlen(s);
    if (len + (size_t)n < cap) {
        memcpy(s + len, b, (size_t)n);
        s[len + n] = 0;
    }
}
static void focus(TuiState *s, int f) {
    s->focus = f;
    s->query[0] = 0;
    s->candidate = 0;
    tui_search(s);
}
int tui_run(const char *path) {
    MapDb *db = calloc(1, sizeof(*db));
    if (!db)
        return -1;
    if (map_db_open(db, path)) {
        fprintf(stderr, "%s\n", db->error);
        free(db);
        return -1;
    }
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE), output = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD imode, omode;
    if (!GetConsoleMode(input, &imode) || !GetConsoleMode(output, &omode)) {
        fprintf(stderr, "地图模式需要交互式终端；使用 --text data 进入文本模式。\n");
        map_db_close(db);
        free(db);
        return -1;
    }
    UINT icp = GetConsoleCP(), ocp = GetConsoleOutputCP();
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
    setlocale(LC_ALL, ".UTF-8");
    interrupted = 0;
    void (*old_handler)(int) = signal(SIGINT, on_signal);
    WINDOW *win = initscr();
    int rc = -1;
    TuiState s;
    MapFrame frame = {0};
    int initialized = 0;
    if (!win)
        goto done;
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    timeout(100);
    colors(db);
    int h, w;
    getmaxyx(stdscr, h, w);
    if (tui_init(&s, db, w, h))
        goto done;
    initialized = 1;
    int dirty = 1;
    while (!interrupted) {
        getmaxyx(stdscr, h, w);
        if (dirty) {
            if (frame_resize(&frame, w, h))
                goto done;
            tui_frame(&s, &frame);
            present(&frame);
            dirty = 0;
        }
        wint_t key;
        int kind = get_wch(&key);
        if (kind == ERR)
            continue;
        if (key == 3)
            break;
        if (key == KEY_RESIZE) {
            resize_term(0, 0);
            dirty = 1;
            continue;
        }
        dirty = 1;
        if ((w < 50 || h < 16) && (key == 'q' || key == 'Q'))
            break;
        if (key == 27) {
            focus(&s, 0);
            continue;
        }
        if (key == '\t' || key == KEY_BTAB) {
            focus(&s, (s.focus + (key == '\t' ? 1 : 3)) % 4);
            continue;
        }
        if (key == KEY_NPAGE || key == KEY_PPAGE) {
            s.scroll += key == KEY_NPAGE ? 5 : -5;
            if (s.scroll < 0)
                s.scroll = 0;
            if (s.scroll >= (int)s.route.stations.size)
                s.scroll = s.route.stations.size ? (int)s.route.stations.size - 1 : 0;
            continue;
        }
        if (s.focus == 1 || s.focus == 2) {
            if (key == KEY_UP) {
                if (s.candidate)
                    s.candidate--;
            } else if (key == KEY_DOWN) {
                if (s.candidate + 1 < s.match_count)
                    s.candidate++;
            } else if (key == '\n' || key == '\r' || key == KEY_ENTER) {
                if (s.match_count) {
                    int id = s.matches[s.candidate];
                    if (s.focus == 1)
                        s.from = id;
                    else
                        s.to = id;
                    s.view.x = db->stations[id].x;
                    s.view.y = db->stations[id].y;
                    if (s.view.scale < .18)
                        s.view.scale = .18;
                    int next = s.focus == 1 ? 2 : 0;
                    focus(&s, next);
                    tui_plan(&s, w, h);
                }
            } else if (key == KEY_BACKSPACE || key == 127 || key == 8) {
                size_t n = strlen(s.query);
                if (n) {
                    n--;
                    while (n && ((unsigned char)s.query[n] & 0xc0) == 0x80)
                        n--;
                    s.query[n] = 0;
                }
                s.candidate = 0;
                tui_search(&s);
            } else if (kind != KEY_CODE_YES) {
                append_utf8(s.query, sizeof(s.query), (unsigned)key);
                s.candidate = 0;
                tui_search(&s);
            }
            continue;
        }
        if (s.focus == 3) {
            if (key == KEY_LEFT || key == KEY_RIGHT || key == '\n' || key == '\r' || key == ' ') {
                s.metric = (s.metric + (key == KEY_LEFT ? 2 : 1)) % 3;
                tui_plan(&s, w, h);
            }
            continue;
        }
        int mw = w >= 90 ? w - 37 : w, mh = h - 3;
        double step = 12 / s.view.scale;
        if (key == 'q' || key == 'Q')
            break;
        if (key == '/') {
            focus(&s, 1);
            continue;
        }
        if (key == KEY_LEFT || key == 'a')
            s.view.x -= step;
        if (key == KEY_RIGHT || key == 'd')
            s.view.x += step;
        if (key == KEY_UP || key == 'w')
            s.view.y -= step;
        if (key == KEY_DOWN || key == 's')
            s.view.y += step;
        if (key == '+' || key == '=')
            viewport_zoom(&s.view, 1.3, mw, mh, mw, mh * 2);
        if (key == '-' || key == '_')
            viewport_zoom(&s.view, 1 / 1.3, mw, mh, mw, mh * 2);
        if (key == 'r' || key == 'R')
            viewport_fit(&s.view, db, NULL, mw, mh);
        if ((key == 'f' || key == 'F') && s.ready)
            viewport_fit(&s.view, db, &s.route, mw, mh);
        if (key == 'x' || key == 'X') {
            int t = s.from;
            s.from = s.to;
            s.to = t;
            tui_plan(&s, w, h);
        }
        if (key == 'c' || key == 'C') {
            s.from = s.to = 0;
            tui_plan(&s, w, h);
            snprintf(s.status, sizeof(s.status), "已清除路线；每区间暂定 1000m / 60s");
        }
    }
    rc = 0;
done:
    frame_dispose(&frame);
    if (initialized)
        tui_dispose(&s);
    if (win)
        endwin();
    signal(SIGINT, old_handler);
    SetConsoleMode(input, imode);
    SetConsoleMode(output, omode);
    SetConsoleCP(icp);
    SetConsoleOutputCP(ocp);
    map_db_close(db);
    free(db);
    return rc;
}
