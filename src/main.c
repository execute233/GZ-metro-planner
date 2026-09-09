#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>
#include "ui/tui.h"

#define MAP_PATH_CAPACITY 32768

#ifdef GZMP_EMBEDDED_DATA
extern const unsigned char _binary_metro_mbtiles_start[];
extern const unsigned char _binary_metro_mbtiles_end[];

/* Publish a complete sibling file atomically, without replacing an existing atlas. */
static int bundle_materialize(const wchar_t *path) {
    wchar_t temporary[MAP_PATH_CAPACITY];
    int length = swprintf(temporary, MAP_PATH_CAPACITY, L"%ls.tmp-%lu-%llu", path,
                          GetCurrentProcessId(), (unsigned long long)GetTickCount64());
    if (length < 0 || length >= MAP_PATH_CAPACITY)
        return -1;
    HANDLE out = CreateFileW(temporary, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                             FILE_ATTRIBUTE_NORMAL, NULL);
    if (out == INVALID_HANDLE_VALUE)
        return -1;
    const unsigned char *data = _binary_metro_mbtiles_start;
    size_t remaining = (uintptr_t)_binary_metro_mbtiles_end - (uintptr_t)data;
    int ok = remaining > 0;
    while (ok && remaining) {
        DWORD count = remaining > 1048576 ? 1048576 : (DWORD)remaining;
        DWORD written = 0;
        ok = WriteFile(out, data, count, &written, NULL) && written > 0;
        data += written;
        remaining -= written;
    }
    if (ok)
        ok = FlushFileBuffers(out) != 0;
    if (!CloseHandle(out))
        ok = 0;
    if (ok && MoveFileExW(temporary, path, MOVEFILE_WRITE_THROUGH))
        return 0;
    DWORD error = GetLastError();
    DeleteFileW(temporary);
    /* Another instance may have finished publishing first. */
    return ok && (error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS) ? 0 : -1;
}
#endif

/* Windows paths are UTF-16; SQLite expects UTF-8. Never depend on the shell's cwd. */
static int resolve_map_path(char *path, int capacity) {
    wchar_t wide[MAP_PATH_CAPACITY];
    DWORD length = GetModuleFileNameW(NULL, wide, MAP_PATH_CAPACITY);
    if (!length || length >= MAP_PATH_CAPACITY)
        return -1;
    wchar_t *slash = wcsrchr(wide, L'\\');
    if (!slash || (size_t)(slash - wide) + wcslen(L"\\metro.mbtiles") >= MAP_PATH_CAPACITY)
        return -1;
    wcscpy(slash + 1, L"metro.mbtiles");
    DWORD attributes = GetFileAttributesW(wide);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        if (GetLastError() != ERROR_FILE_NOT_FOUND)
            return -1;
#ifdef GZMP_EMBEDDED_DATA
        if (bundle_materialize(wide) != 0)
            return -1;
#else
        return -1;
#endif
    } else if (attributes & FILE_ATTRIBUTE_DIRECTORY) {
        return -1;
    }
    return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1, path, capacity,
                               NULL, NULL) ? 0 : -1;
}

static int parse_dimension(const char *arg, int maximum) {
    char *end;
    errno = 0;
    long value = strtol(arg, &end, 10);
    return errno || end == arg || *end || value < 1 || value > maximum ? -1 : (int)value;
}

int main(int argc, char *argv[]) {
    int snapshot = argc > 1 && strcmp(argv[1], "--snapshot") == 0;
    if ((snapshot && ((argc != 5 && argc != 7) || parse_dimension(argv[3], 1000) < 0 ||
                      parse_dimension(argv[4], 500) < 0)) || (!snapshot && argc != 1)) {
        fprintf(stderr, "Usage: GZ_metro_planner\n"
                        "       GZ_metro_planner --snapshot output.json cols rows [from to]\n");
        return 1;
    }
    char path[MAP_PATH_CAPACITY * 4];
    if (resolve_map_path(path, sizeof(path)) != 0) {
        fprintf(stderr, "Cannot open metro.mbtiles beside the EXE. For a bundled release, "
                        "place the EXE in a writable directory; for a development build, rebuild "
                        "to copy the project atlas.\n");
        return 1;
    }
    if (snapshot)
        return tui_snapshot(path, argv[2], parse_dimension(argv[3], 1000), parse_dimension(argv[4], 500),
                            argc == 7 ? argv[5] : NULL, argc == 7 ? argv[6] : NULL) != 0;
    return tui_run(path) != 0;
}
