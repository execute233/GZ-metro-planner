#include "map_db.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "metro_io.h"

int map_db_open(MapDb *m, const char *path) {
    memset(m, 0, sizeof(*m));
    if (sqlite3_open_v2(path, &m->db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK)
        goto bad;
    sqlite3_limit(m->db, SQLITE_LIMIT_LENGTH, 8 * 1024 * 1024);
    sqlite3_db_config(m->db, SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0, NULL);
    sqlite3_stmt *q = NULL;
    if (metro_io_load_db(m->db, &m->metro))
        goto bad;
    for (size_t i = 0; i < m->metro.stations.rows.size; i++) {
        const Station *st = &m->metro.stations.rows.items[i];
        MapStation *p = &m->stations[st->id];
        p->x = st->x;
        p->y = st->y;
        p->transfer = st->transfer;
        memcpy(p->initials, st->initials, sizeof(p->initials));
    }
    for (size_t i = 0; i < m->metro.lines.rows.size; i++) {
        const Line *ln = &m->metro.lines.rows.items[i];
        m->colors[ln->id] = ln->rgb;
    }
    if (sqlite3_prepare_v2(m->db, "SELECT min(zoom_level),max(zoom_level) FROM tiles", -1, &q,
                           NULL) != SQLITE_OK)
        goto bad;
    int valid = sqlite3_step(q) == SQLITE_ROW && sqlite3_column_type(q, 0) != SQLITE_NULL;
    m->minzoom = sqlite3_column_int(q, 0);
    m->maxzoom = sqlite3_column_int(q, 1);
    sqlite3_finalize(q);
    if (!valid || m->minzoom < 0 || m->maxzoom > 14)
        goto bad;
    if (sqlite3_prepare_v2(
            m->db,
            "SELECT tile_data FROM tiles WHERE zoom_level=? AND tile_column=? AND tile_row=?", -1,
            &m->tile_query, NULL) != SQLITE_OK)
        goto bad;
    return 0;
bad:
    snprintf(m->error, sizeof(m->error), "地图数据载入失败：需要 GZMP MBTiles schema 2（%s）",
             m->db ? sqlite3_errmsg(m->db) : "open");
    map_db_close(m);
    return -1;
}
void map_db_close(MapDb *m) {
    for (int i = 0; i < TILE_CACHE_SIZE; i++)
        mvt_dispose(&m->cache[i].segments);
    sqlite3_finalize(m->tile_query);
    m->tile_query = NULL;
    sqlite3_close(m->db);
    m->db = NULL;
    station_table_dispose(&m->metro.stations);
    line_table_dispose(&m->metro.lines);
    edge_table_dispose(&m->metro.edges);
}
int map_db_tile(MapDb *m, int z, int x, int y, const MapSegments **out) {
    *out = NULL;
    if (!m->db || z < 0 || z > 14 || x < 0 || y < 0 || x >= (1 << z) || y >= (1 << z))
        return -1;
    CachedTile *t = &m->cache[0];
    for (int i = 0; i < TILE_CACHE_SIZE; i++) {
        CachedTile *c = &m->cache[i];
        if (c->valid && c->z == z && c->x == x && c->y == y) {
            c->used = ++m->tick;
            *out = &c->segments;
            return c->status;
        }
        if (!c->valid || (t->valid && c->used < t->used))
            t = c;
    }
    mvt_dispose(&t->segments);
    *t = (CachedTile){.z = z, .x = x, .y = y, .valid = 1, .status = -1, .used = ++m->tick};
    sqlite3_stmt *q = m->tile_query;
    sqlite3_reset(q);
    sqlite3_clear_bindings(q);
    sqlite3_bind_int(q, 1, z);
    sqlite3_bind_int(q, 2, x);
    sqlite3_bind_int(q, 3, (1 << z) - 1 - y);
    int rc = sqlite3_step(q);
    if (rc == SQLITE_DONE)
        t->status = 1;
    else if (rc == SQLITE_ROW) {
        const uint8_t *data = sqlite3_column_blob(q, 0);
        int n = sqlite3_column_bytes(q, 0);
        t->status = mvt_decode_blob(data, n > 0 ? (size_t)n : 0, z, x, y, &t->segments);
    }
    sqlite3_reset(q);
    *out = &t->segments;
    return t->status;
}
