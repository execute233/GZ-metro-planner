#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui/tui.h"

/* Default: full-screen SQLite/MBTiles map;
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
    if (argc > 2 || (argc > 1 && argv[1][0] == '-')) {
        fprintf(stderr, "Usage: GZ_metro_planner [data-directory|map.mbtiles]\n"
                        "       GZ_metro_planner --snapshot map.mbtiles output.json cols rows [from to]\n");
        return 1;
    }
    char path[1024];
    const char *arg = argc > 1 ? argv[1] : "data";
    size_t n = strlen(arg);
    int length = snprintf(path, sizeof(path), "%s%s", arg,
                          n >= 8 && !strcmp(arg + n - 8, ".mbtiles") ? "" : "/metro.mbtiles");
    if (length < 0 || (size_t)length >= sizeof(path)) {
        fprintf(stderr, "Map path is too long\n");
        return 1;
    }
    return tui_run(path) != 0;
}
