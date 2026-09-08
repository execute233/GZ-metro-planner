#include "map_db.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>

static int copy_text(sqlite3_stmt *q, int column, char *dst, size_t capacity) {
    const unsigned char *s = sqlite3_column_text(q, column);
    int n = sqlite3_column_bytes(q, column);
    if (!s || n < 0 || (size_t)n >= capacity || memchr(s, 0, (size_t)n))
        return -1;
    for (int i = 0; i < n; i++)
        if (s[i] < 32 || s[i] == 127)
            return -1;
    memcpy(dst, s, (size_t)n + 1);
    return 0;
}
static int load_model(MapDb *m) {
    sqlite3_stmt *q = NULL;
    int rc;
    if (sqlite3_prepare_v2(m->db,
                           "SELECT id,name,pinyin,initials,x,y,transfer FROM stations ORDER BY id",
                           -1, &q, NULL) != SQLITE_OK)
        return -1;
    while ((rc = sqlite3_step(q)) == SQLITE_ROW) {
        Station st = {0};
        st.id = sqlite3_column_int(q, 0);
        if (st.id < 1 || st.id >= MAP_LIMIT || station_find_by_id(&m->metro.stations, st.id))
            goto bad;
        MapStation *p = &m->stations[st.id];
        if (copy_text(q, 1, st.name, sizeof(st.name)) ||
            copy_text(q, 2, st.pinyin_name, sizeof(st.pinyin_name)) ||
            copy_text(q, 3, p->initials, sizeof(p->initials)))
            goto bad;
        p->x = sqlite3_column_double(q, 4);
        p->y = sqlite3_column_double(q, 5);
        p->transfer = sqlite3_column_int(q, 6) != 0;
        if (!isfinite(p->x) || !isfinite(p->y) || p->x < 0 || p->y < 0 || p->x > 4096 ||
            p->y > 4096)
            goto bad;
        if (al_station_push(&m->metro.stations.rows, st))
            goto bad;
    }
    if (rc != SQLITE_DONE || !m->metro.stations.rows.size)
        goto bad;
    sqlite3_finalize(q);
    q = NULL;
    if (sqlite3_prepare_v2(m->db, "SELECT id,name,color FROM lines ORDER BY id", -1, &q, NULL) !=
        SQLITE_OK)
        return -1;
    while ((rc = sqlite3_step(q)) == SQLITE_ROW) {
        Line ln = {0};
        ln.id = sqlite3_column_int(q, 0);
        ln.color = 37;
        if (ln.id < 1 || ln.id >= 128 || line_find_by_id(&m->metro.lines, ln.id) ||
            copy_text(q, 1, ln.name, sizeof(ln.name)))
            goto bad;
        sqlite3_int64 rgb = sqlite3_column_int64(q, 2);
        if (rgb < 0 || rgb > 0xffffff)
            goto bad;
        m->colors[ln.id] = (unsigned)rgb;
        if (al_line_push(&m->metro.lines.rows, ln))
            goto bad;
    }
    if (rc != SQLITE_DONE)
        goto bad;
    sqlite3_finalize(q);
    q = NULL;
    if (sqlite3_prepare_v2(m->db,
                           "SELECT id,line_id,from_id,to_id,seconds,meters FROM edges ORDER BY id",
                           -1, &q, NULL) != SQLITE_OK)
        return -1;
    while ((rc = sqlite3_step(q)) == SQLITE_ROW) {
        Edge e = {sqlite3_column_int(q, 0), sqlite3_column_int(q, 1), sqlite3_column_int(q, 2),
                  sqlite3_column_int(q, 3), sqlite3_column_int(q, 4), sqlite3_column_int(q, 5)};
        if (e.id < 1 || e.id >= MAP_LIMIT * 4 || edge_find_by_id(&m->metro.edges, e.id) ||
            !line_find_by_id(&m->metro.lines, e.line_id) ||
            !station_find_by_id(&m->metro.stations, e.from_station_id) ||
            !station_find_by_id(&m->metro.stations, e.to_station_id) ||
            e.from_station_id == e.to_station_id || e.cost_time_second < 0 ||
            e.cost_time_second > 100000 || e.cost_meters < 1 || e.cost_meters > 100000)
            goto bad;
        if (al_edge_push(&m->metro.edges.rows, e))
            goto bad;
    }
    if (rc != SQLITE_DONE)
        goto bad;
    sqlite3_finalize(q);
    return 0;
bad:
    sqlite3_finalize(q);
    return -1;
}
int map_db_open(MapDb *m, const char *path) {
    memset(m, 0, sizeof(*m));
    if (sqlite3_open_v2(path, &m->db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK)
        goto bad;
    sqlite3_limit(m->db, SQLITE_LIMIT_LENGTH, 8 * 1024 * 1024);
    sqlite3_db_config(m->db, SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0, NULL);
    sqlite3_stmt *q = NULL;
    if (sqlite3_prepare_v2(m->db, "SELECT value FROM metadata WHERE name='gzmp_schema'", -1, &q,
                           NULL) != SQLITE_OK)
        goto bad;
    int valid = sqlite3_step(q) == SQLITE_ROW && sqlite3_column_int(q, 0) == 1;
    sqlite3_finalize(q);
    if (!valid || load_model(m))
        goto bad;
    if (sqlite3_prepare_v2(m->db, "SELECT min(zoom_level),max(zoom_level) FROM tiles", -1, &q,
                           NULL) != SQLITE_OK)
        goto bad;
    valid = sqlite3_step(q) == SQLITE_ROW && sqlite3_column_type(q, 0) != SQLITE_NULL;
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
    snprintf(m->error, sizeof(m->error), "地图数据载入失败：需要有效的 GZMP MBTiles（%s）",
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
        uint8_t *raw = NULL;
        size_t size = n > 0 ? (size_t)n : 0;
        if (n > 1 && n <= 4 * 1024 * 1024 && data[0] == 31 && data[1] == 139) {
            raw = malloc(4 * 1024 * 1024);
            if (raw) {
                z_stream s = {0};
                s.next_in = (Bytef *)data;
                s.avail_in = (unsigned)n;
                s.next_out = raw;
                s.avail_out = 4 * 1024 * 1024;
                if (inflateInit2(&s, 31) == Z_OK) {
                    int ok = inflate(&s, Z_FINISH) == Z_STREAM_END && !s.avail_in;
                    size = s.total_out;
                    inflateEnd(&s);
                    if (ok)
                        t->status = mvt_decode(raw, size, z, x, y, &t->segments);
                }
            }
        } else if (size)
            t->status = mvt_decode(data, size, z, x, y, &t->segments);
        free(raw);
    }
    sqlite3_reset(q);
    *out = &t->segments;
    return t->status;
}
