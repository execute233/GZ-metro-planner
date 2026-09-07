#include "tui_backend.h"
#include <windows.h>
#include <curses.h>
#include <signal.h>
#include <stdlib.h>

static volatile sig_atomic_t interrupted;
static bool active;
static SCREEN *screen;
static DWORD old_input, old_output;
static UINT old_input_cp, old_output_cp;
static uint32_t colors[240];
static int color_count;
static wchar_t high_surrogate;
static BOOL WINAPI on_console_control(DWORD control) {
    if (control == CTRL_C_EVENT || control == CTRL_BREAK_EVENT) { interrupted = 1; return TRUE; }
    return FALSE;
}
static void on_signal(int sig) { (void)sig; interrupted = 1; }
void tui_backend_dispose(void) {
    if (!active) return;
    endwin(); delscreen(screen); active = false;
    SetConsoleCtrlHandler(on_console_control, FALSE);
    SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), old_input);
    SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), old_output);
    SetConsoleCP(old_input_cp); SetConsoleOutputCP(old_output_cp);
    signal(SIGINT, SIG_DFL); signal(SIGTERM, SIG_DFL);
}
int tui_backend_init(void) {
    if (!GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &old_input) ||
        !GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &old_output)) return -1;
    old_input_cp = GetConsoleCP(); old_output_cp = GetConsoleOutputCP();
    SetConsoleCP(CP_UTF8); SetConsoleOutputCP(CP_UTF8);
    screen = newterm(NULL, stdout, stdin);
    if (!screen) {
        SetConsoleCP(old_input_cp); SetConsoleOutputCP(old_output_cp);
        SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), old_input);
        SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), old_output); return -1;
    }
    active = true; atexit(tui_backend_dispose);
    interrupted = 0; color_count = 0; high_surrogate = 0;
    SetConsoleCtrlHandler(on_console_control, TRUE);
    cbreak(); noecho(); keypad(stdscr, TRUE); curs_set(0); timeout(100);
    start_color(); mousemask(ALL_MOUSE_EVENTS, NULL); mouseinterval(0);
    signal(SIGINT, on_signal); signal(SIGTERM, on_signal);
    return 0;
}
void tui_backend_size(int *cols, int *rows) { getmaxyx(stdscr, *rows, *cols); }
TuiEvent tui_backend_event(void) {
    TuiEvent e = {0};
    if (interrupted) { e.type = EVENT_QUIT; return e; }
    wint_t ch; int kind = get_wch(&ch);
    if (kind == ERR) return e;
    if (kind == KEY_CODE_YES) {
        if (ch == KEY_RESIZE) { resize_term(0, 0); e.type = EVENT_RESIZE; return e; }
        if (ch == KEY_MOUSE) {
            MEVENT m;
            if (nc_getmouse(&m) == OK) {
                e.type = EVENT_MOUSE; e.x = m.x; e.y = m.y;
                e.click = (m.bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED)) != 0;
                e.wheel = m.bstate & BUTTON4_PRESSED ? 1 : m.bstate & BUTTON5_PRESSED ? -1 : 0;
            }
            return e;
        }
        e.type = EVENT_KEY;
        switch (ch) {
            case KEY_UP: e.key = TKEY_UP; break; case KEY_DOWN: e.key = TKEY_DOWN; break;
            case KEY_LEFT: e.key = TKEY_LEFT; break; case KEY_RIGHT: e.key = TKEY_RIGHT; break;
            case KEY_BACKSPACE: e.key = TKEY_BACKSPACE; break; case KEY_DC: e.key = TKEY_DELETE; break;
            case KEY_HOME: e.key = TKEY_HOME; break; case KEY_END: e.key = TKEY_END; break;
            case KEY_NPAGE: e.key = TKEY_PAGEDOWN; break; case KEY_PPAGE: e.key = TKEY_PAGEUP; break;
            case KEY_ENTER: e.key = TKEY_ENTER; break;
            default: e.type = EVENT_NONE; break;
        }
    } else {
        e.type = EVENT_KEY;
        if (ch == 27) e.key = TKEY_ESCAPE;
        else if (ch == 9) e.key = TKEY_TAB;
        else if (ch == 13 || ch == 10) e.key = TKEY_ENTER;
        else if (ch == 8 || ch == 127) e.key = TKEY_BACKSPACE;
        else {
            if (ch >= 0xd800 && ch <= 0xdbff) { high_surrogate = (wchar_t)ch; return (TuiEvent){0}; }
            e.type = EVENT_TEXT; e.text = (uint32_t)ch;
            if (ch >= 0xdc00 && ch <= 0xdfff && high_surrogate) e.text = 0x10000+((high_surrogate-0xd800)<<10)+(ch-0xdc00);
            high_surrogate = 0;
        }
    }
    return e;
}
static int pair_for(uint32_t rgb) {
    if (!has_colors()) return 0;
    for (int i = 0; i < color_count; i++) if (colors[i] == rgb) return i+1;
    if (color_count >= 240 || color_count + 1 >= COLOR_PAIRS) return 0;
    int slot = color_count++, index = slot + 16;
    colors[slot] = rgb;
    if (can_change_color() && index < COLORS) {
        init_color((short)index, (short)(((rgb>>16)&255)*1000/255), (short)(((rgb>>8)&255)*1000/255), (short)((rgb&255)*1000/255));
    } else {
        index = ((rgb>>16)&255) > 127 ? COLOR_RED : 0;
        if (((rgb>>8)&255) > 127) index |= COLOR_GREEN;
        if ((rgb&255) > 127) index |= COLOR_BLUE;
    }
    init_pair((short)(slot+1), (short)index, COLOR_BLACK);
    return slot+1;
}
void tui_backend_present(const CellSurface *s) {
    for (int y = 0; y < s->rows; y++) for (int x = 0; x < s->cols; x++) {
        const Cell *c = &s->cells[y*s->cols+x];
        if (c->continuation) continue;
        wchar_t text[3] = {0};
        if (c->cp <= 0xffff) text[0] = (wchar_t)c->cp;
        else { text[0] = (wchar_t)(0xd800+((c->cp-0x10000)>>10)); text[1] = (wchar_t)(0xdc00+((c->cp-0x10000)&1023)); }
        cchar_t out;
        setcchar(&out, text, A_NORMAL, (short)pair_for(c->rgb), NULL);
        mvadd_wch(y, x, &out);
    }
    wnoutrefresh(stdscr); doupdate();
}
