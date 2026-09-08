#ifndef GZMP_MAP_DB_H
#define GZMP_MAP_DB_H
#include "../metro.h"
#include "mvt.h"
#include <sqlite3.h>
#define MAP_LIMIT 4096
#define TILE_CACHE_SIZE 24
typedef struct {
    double x, y;
    char initials[65];
    int transfer;
} MapStation;
typedef struct {
    int z, x, y, valid, status;
    unsigned long used;
    MapSegments segments;
} CachedTile;
typedef struct {
    sqlite3 *db;
    sqlite3_stmt *tile_query;
    Metro metro;
    MapStation stations[MAP_LIMIT];
    unsigned colors[128];
    CachedTile cache[TILE_CACHE_SIZE];
    unsigned long tick;
    int minzoom, maxzoom;
    char error[256];
} MapDb;
int map_db_open(MapDb *map, const char *path);
void map_db_close(MapDb *map);
/* 0 found, 1 absent, -1 corrupt. Pointer valid until next cache eviction. */
int map_db_tile(MapDb *map, int z, int x, int y, const MapSegments **out);
#endif
