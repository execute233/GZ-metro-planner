#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <windows.h>
#include "ui/tui.h"

/* 分发包把 metro.mbtiles 编译进 exe；运行时才把它展开到 exe 旁。 */
#ifdef GZMP_EMBEDDED_DATA
/* Supplied by the data object appended at link time (see cmake/PackagedApp.cmake). */
extern const unsigned char _binary_metro_mbtiles_start[];
extern const unsigned char _binary_metro_mbtiles_end[];
static long bundle_size(void) {
    return (long)(_binary_metro_mbtiles_end - _binary_metro_mbtiles_start);
}
#endif
/* Resolve <exe folder>/leaf into buffer; NULL on failure. */
static const char *exe_sibling(char *buffer, size_t capacity, const char *leaf) {
    if (!GetModuleFileNameA(NULL, buffer, (DWORD)capacity) || capacity <= 1 || !buffer[0])
        return NULL;
    char *slash = NULL;
    for (char *p = buffer; *p; p++)
        if (*p == '\\' || *p == '/')
            slash = p;
    if (!slash)
        return NULL;
    slash[1] = 0;
    if (strlen(buffer) + strlen(leaf) >= capacity)
        return NULL;
    strcat(buffer, leaf);
    return buffer;
}

static int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

/* Write the bundled map into <exe folder>/metro.mbtiles on first run.
 * Never overwrite an existing map and remove a partial file on failure. */
static int bundle_materialize(const char *path) {
#ifdef GZMP_EMBEDDED_DATA
    if (file_exists(path))
        return 0;
    const unsigned char *data = _binary_metro_mbtiles_start;
    long remaining = bundle_size();
    FILE *out = fopen(path, "wb");
    if (!out)
        return -1;
    int ok = 1;
    while (remaining > 0) {
        size_t written = fwrite(data, 1, (size_t)remaining, out);
        if (!written) {
            ok = 0;
            break;
        }
        data += written;
        remaining -= (long)written;
    }
    if (ok && fclose(out) != 0)
        ok = 0;
    if (!ok) {
        remove(path);
        return -1;
    }
    return 0;
#else
    (void)path;
    return -1;
#endif
}

/* Resolve where a packaged exe keeps its map: always the folder next to the
 * exe. Materialize the bundled map there on first run; never write anywhere
 * else, even if that folder is read-only. Fills `path` on success. */
static const char *resolve_packaged_data(char *path, size_t capacity) {
    if (exe_sibling(path, capacity, "metro.mbtiles")) {
        if (bundle_materialize(path) == 0 || file_exists(path))
            return path;
    }
    return NULL;
}

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
#ifdef GZMP_EMBEDDED_DATA
    /* Release build: map data is compiled in and released next to the exe on
     * first run. Accept the usual explicit <dir|file> override as well. */
    if (argc > 1) {
        char explicit_path[1024];
        size_t n = strlen(argv[1]);
        int length = snprintf(explicit_path, sizeof(explicit_path), "%s%s", argv[1],
                              n >= 8 && !strcmp(argv[1] + n - 8, ".mbtiles") ? "" : "/metro.mbtiles");
        if (length < 0 || (size_t)length >= sizeof(explicit_path)) {
            fprintf(stderr, "Map path is too long\n");
            return 1;
        }
        return tui_run(explicit_path) != 0;
    }
    char data_path[MAX_PATH];
    const char *path = resolve_packaged_data(data_path, sizeof(data_path));
    if (path)
        return tui_run(path) != 0;
    fprintf(stderr, "无法写入初始地图 metro.mbtiles，请把程序放在可写目录后重试\n");
    return 1;
#else
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
#endif
}
