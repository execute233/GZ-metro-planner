#include <locale.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include "ui/ui.h"
#include "ui/tui.h"

/* Default: full-screen MBTiles map. --text preserves the legacy CSV menu;
 * --snapshot exports the same cell renderer without initializing a terminal. */
static int parse_dimension(const char *arg, int maximum) {
    char *end;
    errno = 0;
    long value = strtol(arg, &end, 10);
    return errno || end == arg || *end || value < 1 || value > maximum ? -1 : (int)value;
}
int main(int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "--snapshot") == 0) {
        if ((argc != 6 && argc != 8) || parse_dimension(argv[4], 1000) < 0 ||
            parse_dimension(argv[5], 500) < 0) {
            fprintf(stderr, "Usage: --snapshot map.mbtiles output.json cols rows [from to]\n");
            return 1;
        }
        return tui_snapshot(argv[2], argv[3], parse_dimension(argv[4], 1000), parse_dimension(argv[5], 500),
                            argc > 7 ? argv[6] : NULL, argc > 7 ? argv[7] : NULL) != 0;
    }
    if (argc <= 1 || strcmp(argv[1], "--text") != 0) {
        char path[1024];
        const char *arg = argc > 1 ? argv[1] : "data";
        size_t n = strlen(arg);
        if (n + sizeof("/metro.mbtiles") > sizeof(path)) {
            fprintf(stderr, "Map path is too long\n");
            return 1;
        }
        if (n > 8 && strcmp(arg + n - 8, ".mbtiles") == 0)
            snprintf(path, sizeof(path), "%s", arg);
        else
            snprintf(path, sizeof(path), "%s/metro.mbtiles", arg);
        return tui_run(path) != 0;
    }
#ifdef _WIN32
    SetConsoleOutputCP(65001);   /* 控制台输出 UTF-8 */
    SetConsoleCP(65001);         /* 控制台输入 UTF-8 */
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(hOut, &mode))
        SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif
    setlocale(LC_ALL, ".UTF-8");

    const char *data_dir = (argc > 2) ? argv[2] : "data";
    return ui_main_loop(data_dir) == 0 ? 0 : 1;
}
