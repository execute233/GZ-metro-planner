#include "metro_io.h"
#include "tile_writer.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int valid_text(const char *text, size_t capacity) {
    const char *end = memchr(text, 0, capacity);
    if (!end)
        return 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++)
        if (*p < 32 || *p == 127)
            return 0;
    return 1;
}

static int copy_text(sqlite3_stmt *q, int col, char *dst, size_t capacity) {
    const unsigned char *text = sqlite3_column_text(q, col);
    int size = sqlite3_column_bytes(q, col);
    if (!text || size < 0 || (size_t)size >= capacity || memchr(text, 0, (size_t)size))
        return -1;
    memcpy(dst, text, (size_t)size + 1);
    return valid_text(dst, capacity) ? 0 : -1;
}

static int integer(sqlite3_stmt *q, int col, int min, int max, int *out) {
    sqlite3_int64 value = sqlite3_column_int64(q, col);
    if (sqlite3_column_type(q, col) != SQLITE_INTEGER || value < min || value > max)
        return -1;
    *out = (int)value;
    return 0;
}

static void dispose(Metro *metro) {
    station_table_dispose(&metro->stations);
    line_table_dispose(&metro->lines);
    edge_table_dispose(&metro->edges);
}

int metro_io_path(const char *arg, char *path, size_t capacity) {
    size_t size = strlen(arg);
    int n = snprintf(path, capacity, "%s%s", arg,
                     size >= 8 && !strcmp(arg + size - 8, ".mbtiles") ? "" : "/metro.mbtiles");
    return n < 0 || (size_t)n >= capacity ? -1 : 0;
}

static int read_stations(sqlite3 *db, Metro *metro) {
    sqlite3_stmt *q = NULL;
    int rc, result = -1;
    if (sqlite3_prepare_v2(db, "SELECT id,name,pinyin,en_name,x,y,initials,transfer FROM stations ORDER BY id",
                           -1, &q, NULL) != SQLITE_OK)
        return -1;
    while ((rc = sqlite3_step(q)) == SQLITE_ROW) {
        Station st = {0};
        if (integer(q, 0, 1, METRO_STATION_LIMIT - 1, &st.id) ||
            copy_text(q, 1, st.name, sizeof(st.name)) ||
            copy_text(q, 2, st.pinyin_name, sizeof(st.pinyin_name)) ||
            copy_text(q, 3, st.en_name, sizeof(st.en_name)) ||
            copy_text(q, 6, st.initials, sizeof(st.initials)) ||
            integer(q, 7, 0, 1, &st.transfer))
            goto done;
        if ((sqlite3_column_type(q, 4) != SQLITE_FLOAT && sqlite3_column_type(q, 4) != SQLITE_INTEGER) ||
            (sqlite3_column_type(q, 5) != SQLITE_FLOAT && sqlite3_column_type(q, 5) != SQLITE_INTEGER))
            goto done;
        st.x = sqlite3_column_double(q, 4);
        st.y = sqlite3_column_double(q, 5);
        if (metro->stations.rows.size >= METRO_STATION_LIMIT - 1 ||
            al_station_push(&metro->stations.rows, st))
            goto done;
    }
    result = rc == SQLITE_DONE ? 0 : -1;
done:
    sqlite3_finalize(q);
    return result;
}

static int read_lines(sqlite3 *db, Metro *metro) {
    sqlite3_stmt *q = NULL;
    int rc, result = -1;
    if (sqlite3_prepare_v2(db, "SELECT id,name,en_name,ansi_color,color FROM lines ORDER BY id",
                           -1, &q, NULL) != SQLITE_OK)
        return -1;
    while ((rc = sqlite3_step(q)) == SQLITE_ROW) {
        Line ln = {0};
        int rgb;
        if (integer(q, 0, 1, METRO_LINE_LIMIT - 1, &ln.id) ||
            copy_text(q, 1, ln.name, sizeof(ln.name)) ||
            copy_text(q, 2, ln.en_name, sizeof(ln.en_name)) ||
            integer(q, 3, 30, 37, &ln.color) || integer(q, 4, 0, 0xffffff, &rgb))
            goto done;
        ln.rgb = (unsigned)rgb;
        if (metro->lines.rows.size >= METRO_LINE_LIMIT - 1 || al_line_push(&metro->lines.rows, ln))
            goto done;
    }
    result = rc == SQLITE_DONE ? 0 : -1;
done:
    sqlite3_finalize(q);
    return result;
}

static int read_edges(sqlite3 *db, Metro *metro) {
    sqlite3_stmt *q = NULL;
    int rc, result = -1;
    if (sqlite3_prepare_v2(db, "SELECT id,line_id,from_id,to_id,seconds,meters FROM edges ORDER BY id",
                           -1, &q, NULL) != SQLITE_OK)
        return -1;
    while ((rc = sqlite3_step(q)) == SQLITE_ROW) {
        Edge edge = {0};
        if (integer(q, 0, 1, METRO_EDGE_LIMIT - 1, &edge.id) ||
            integer(q, 1, 1, METRO_LINE_LIMIT - 1, &edge.line_id) ||
            integer(q, 2, 1, METRO_STATION_LIMIT - 1, &edge.from_station_id) ||
            integer(q, 3, 1, METRO_STATION_LIMIT - 1, &edge.to_station_id) ||
            integer(q, 4, 0, 100000, &edge.cost_time_second) ||
            integer(q, 5, 1, 100000, &edge.cost_meters))
            goto done;
        if (metro->edges.rows.size >= METRO_EDGE_LIMIT - 1 || al_edge_push(&metro->edges.rows, edge))
            goto done;
    }
    result = rc == SQLITE_DONE ? 0 : -1;
done:
    sqlite3_finalize(q);
    return result;
}

/* Membership is derived from edges; branches and disconnected sections do not
 * imply an extra connection between successive station_ids. */
static int build_membership(Metro *metro) {
    for (size_t i = 0; i < metro->edges.rows.size; i++) {
        const Edge *edge = &metro->edges.rows.items[i];
        Line *ln = line_find_by_id(&metro->lines, edge->line_id);
        if (!ln)
            return -1;
        const int ids[] = {edge->from_station_id, edge->to_station_id};
        for (int j = 0; j < 2; j++) {
            size_t k = 0;
            while (k < ln->station_ids.size && ln->station_ids.items[k] != ids[j])
                k++;
            if (k == ln->station_ids.size && al_int_push(&ln->station_ids, ids[j]))
                return -1;
        }
    }
    return 0;
}

int metro_io_load_db(sqlite3 *db, Metro *metro) {
    sqlite3_stmt *q = NULL;
    if (sqlite3_prepare_v2(db, "SELECT value FROM metadata WHERE name='gzmp_schema'", -1, &q, NULL) != SQLITE_OK)
        return -1;
    int valid = sqlite3_step(q) == SQLITE_ROW && sqlite3_column_int(q, 0) == 2;
    sqlite3_finalize(q);
    if (!valid)
        return -1;
    Metro loaded = {0};
    char error[256];
    if (read_stations(db, &loaded) || read_lines(db, &loaded) || read_edges(db, &loaded) ||
        build_membership(&loaded) || metro_io_validate(&loaded, error, sizeof(error))) {
        dispose(&loaded);
        return -1;
    }
    dispose(metro);
    *metro = loaded;
    return 0;
}

int metro_io_load(const char *data_path, Metro *metro) {
    char path[1024];
    sqlite3 *db = NULL;
    int result = -1;
    if (metro_io_path(data_path, path, sizeof(path)))
        return -1;
    if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, NULL) == SQLITE_OK) {
        sqlite3_db_config(db, SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0, NULL);
        sqlite3_limit(db, SQLITE_LIMIT_LENGTH, 8 * 1024 * 1024);
        if (sqlite3_exec(db, "BEGIN", NULL, NULL, NULL) == SQLITE_OK)
            result = metro_io_load_db(db, metro);
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
    }
    sqlite3_close(db);
    return result;
}

/* ---- 校验 ---- */

/* 三表主键 id 是否唯一（两两比较，规模小直接 O(n²)） */
static int ids_unique(const StationTable *stations, const LineTable *lines,
                      const EdgeTable *edges) {
    for (size_t i = 0; i < stations->rows.size; i++)
        for (size_t j = i + 1; j < stations->rows.size; j++)
            if (stations->rows.items[i].id == stations->rows.items[j].id)
                return 0;
    for (size_t i = 0; i < lines->rows.size; i++)
        for (size_t j = i + 1; j < lines->rows.size; j++)
            if (lines->rows.items[i].id == lines->rows.items[j].id)
                return 0;
    for (size_t i = 0; i < edges->rows.size; i++)
        for (size_t j = i + 1; j < edges->rows.size; j++)
            if (edges->rows.items[i].id == edges->rows.items[j].id)
                return 0;
    return 1;
}

int metro_io_validate(const Metro *metro, char *errbuf, size_t errbuf_size) {
    const StationTable *stations = &metro->stations;
    const LineTable *lines = &metro->lines;
    const EdgeTable *edges = &metro->edges;

    for (size_t i = 0; i < stations->rows.size; i++) {
        const Station *s = &stations->rows.items[i];
        if (!valid_text(s->name, sizeof(s->name)) || !s->name[0] ||
            !valid_text(s->pinyin_name, sizeof(s->pinyin_name)) ||
            !valid_text(s->initials, sizeof(s->initials)) ||
            !valid_text(s->en_name, sizeof(s->en_name)) ||
            !isfinite(s->x) || !isfinite(s->y) || s->x < 0 || s->y < 0 ||
            s->x > 4096 || s->y > 4096) {
            snprintf(errbuf, errbuf_size, "站点名称或示意坐标非法");
            return -1;
        }
    }
    for (size_t i = 0; i < lines->rows.size; i++) {
        const Line *ln = &lines->rows.items[i];
        if (!valid_text(ln->name, sizeof(ln->name)) || !ln->name[0] ||
            !valid_text(ln->en_name, sizeof(ln->en_name)) || ln->rgb > 0xffffff ||
            ln->color < 30 || ln->color > 37) {
            snprintf(errbuf, errbuf_size, "线路名称或颜色非法");
            return -1;
        }
    }
    if (!ids_unique(stations, lines, edges)) {
        snprintf(errbuf, errbuf_size, "主键 id 重复");
        return -1;
    }
    for (size_t i = 0; i < stations->rows.size; i++) {
        if (stations->rows.items[i].id < 1 || stations->rows.items[i].id >= METRO_STATION_LIMIT) {
            snprintf(errbuf, errbuf_size, "站点 id 必须为正数（第 %zu 条）", i + 1);
            return -1;
        }
    }
    for (size_t i = 0; i < lines->rows.size; i++) {
        if (lines->rows.items[i].id < 1 || lines->rows.items[i].id >= METRO_LINE_LIMIT) {
            snprintf(errbuf, errbuf_size, "线路 id 必须为正数（第 %zu 条）", i + 1);
            return -1;
        }
    }
    for (size_t i = 0; i < edges->rows.size; i++) {
        if (edges->rows.items[i].id < 1 || edges->rows.items[i].id >= METRO_EDGE_LIMIT ||
            edges->rows.items[i].line_id < 1) {
            snprintf(errbuf, errbuf_size, "边 id/line_id 必须为正数（第 %zu 条）", i + 1);
            return -1;
        }
    }
    for (size_t i = 0; i < lines->rows.size; i++) {
        const Line *ln = &lines->rows.items[i];
        for (size_t k = 0; k < ln->station_ids.size; k++) {
            if (station_find_by_id(stations, ln->station_ids.items[k]) == NULL) {
                snprintf(errbuf, errbuf_size, "线路 %s 引用不存在的站 id=%d",
                         ln->name, ln->station_ids.items[k]);
                return -1;
            }
        }
    }
    for (size_t i = 0; i < edges->rows.size; i++) {
        const Edge *e = &edges->rows.items[i];
        if (station_find_by_id(stations, e->from_station_id) == NULL ||
            station_find_by_id(stations, e->to_station_id) == NULL) {
            snprintf(errbuf, errbuf_size, "边 %d 引用不存在的站", e->id);
            return -1;
        }
        Line *ln = line_find_by_id(lines, e->line_id);
        if (ln == NULL) {
            snprintf(errbuf, errbuf_size, "边 %d 引用不存在的线路 %d", e->id, e->line_id);
            return -1;
        }
        if (e->from_station_id == e->to_station_id || e->cost_meters <= 0 ||
            e->cost_meters > 100000 || e->cost_time_second < 0 || e->cost_time_second > 100000) {
            snprintf(errbuf, errbuf_size, "边 %d 里程或时长非法", e->id);
            return -1;
        }
    }
    for (size_t i = 0; i < edges->rows.size; i++) {
        const Edge *a = &edges->rows.items[i];
        for (size_t j = i + 1; j < edges->rows.size; j++) {
            const Edge *b = &edges->rows.items[j];
            if (a->line_id == b->line_id &&
                ((a->from_station_id == b->from_station_id &&
                  a->to_station_id == b->to_station_id) ||
                 (a->from_station_id == b->to_station_id &&
                  a->to_station_id == b->from_station_id))) {
                snprintf(errbuf, errbuf_size, "线路 %d 存在重复区间边 %d/%d",
                         a->line_id, a->id, b->id);
                return -1;
            }
        }
    }
    return 0;
}

static int write_stations(sqlite3 *db, const StationTable *table) {
    sqlite3_stmt *q = NULL;
    if (sqlite3_prepare_v2(db, "INSERT INTO stations(id,name,pinyin,initials,x,y,transfer,en_name) "
                              "VALUES(?,?,?,?,?,?,0,?)", -1, &q, NULL) != SQLITE_OK)
        return -1;
    int result = -1;
    for (size_t i = 0; i < table->rows.size; i++) {
        const Station *st = &table->rows.items[i];
        if (sqlite3_bind_int(q, 1, st->id) != SQLITE_OK ||
            sqlite3_bind_text(q, 2, st->name, -1, SQLITE_STATIC) != SQLITE_OK ||
            sqlite3_bind_text(q, 3, st->pinyin_name, -1, SQLITE_STATIC) != SQLITE_OK ||
            sqlite3_bind_text(q, 4, st->initials, -1, SQLITE_STATIC) != SQLITE_OK ||
            sqlite3_bind_double(q, 5, st->x) != SQLITE_OK ||
            sqlite3_bind_double(q, 6, st->y) != SQLITE_OK ||
            sqlite3_bind_text(q, 7, st->en_name, -1, SQLITE_STATIC) != SQLITE_OK ||
            sqlite3_step(q) != SQLITE_DONE)
            goto done;
        sqlite3_reset(q);
    }
    result = 0;
done:
    sqlite3_finalize(q);
    return result;
}

static int write_lines(sqlite3 *db, const LineTable *table) {
    sqlite3_stmt *q = NULL;
    if (sqlite3_prepare_v2(db, "INSERT INTO lines(id,name,color,en_name,ansi_color) VALUES(?,?,?,?,?)",
                           -1, &q, NULL) != SQLITE_OK)
        return -1;
    int result = -1;
    for (size_t i = 0; i < table->rows.size; i++) {
        const Line *ln = &table->rows.items[i];
        if (sqlite3_bind_int(q, 1, ln->id) != SQLITE_OK ||
            sqlite3_bind_text(q, 2, ln->name, -1, SQLITE_STATIC) != SQLITE_OK ||
            sqlite3_bind_int(q, 3, (int)ln->rgb) != SQLITE_OK ||
            sqlite3_bind_text(q, 4, ln->en_name, -1, SQLITE_STATIC) != SQLITE_OK ||
            sqlite3_bind_int(q, 5, ln->color) != SQLITE_OK || sqlite3_step(q) != SQLITE_DONE)
            goto done;
        sqlite3_reset(q);
    }
    result = 0;
done:
    sqlite3_finalize(q);
    return result;
}

static int write_edges(sqlite3 *db, const EdgeTable *table) {
    sqlite3_stmt *q = NULL;
    if (sqlite3_prepare_v2(db, "INSERT INTO edges(id,line_id,from_id,to_id,seconds,meters) VALUES(?,?,?,?,?,?)",
                           -1, &q, NULL) != SQLITE_OK)
        return -1;
    int result = -1;
    for (size_t i = 0; i < table->rows.size; i++) {
        const Edge *edge = &table->rows.items[i];
        const int values[] = {edge->id, edge->line_id, edge->from_station_id,
                              edge->to_station_id, edge->cost_time_second, edge->cost_meters};
        for (int j = 0; j < 6; j++)
            if (sqlite3_bind_int(q, j + 1, values[j]) != SQLITE_OK)
                goto done;
        if (sqlite3_step(q) != SQLITE_DONE)
            goto done;
        sqlite3_reset(q);
    }
    result = 0;
done:
    sqlite3_finalize(q);
    return result;
}

int metro_io_save(const char *data_path, const Metro *metro) {
    char path[1024], error[256];
    if (metro_io_path(data_path, path, sizeof(path)) || metro_io_validate(metro, error, sizeof(error)))
        return -1;
    sqlite3 *db = NULL;
    Metro before = {0};
    int result = -1;
    /* Never create an empty replacement database on a mistyped path. */
    if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE, NULL) != SQLITE_OK)
        goto done;
    sqlite3_db_config(db, SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0, NULL);
    sqlite3_limit(db, SQLITE_LIMIT_LENGTH, 8 * 1024 * 1024);
    sqlite3_busy_timeout(db, 1000);
    if (sqlite3_exec(db, "PRAGMA foreign_keys=ON; BEGIN IMMEDIATE", NULL, NULL, NULL) != SQLITE_OK ||
        metro_io_load_db(db, &before) || tile_writer_update(db, &before, metro) ||
        sqlite3_exec(db, "DELETE FROM edges; DELETE FROM lines; DELETE FROM stations", NULL, NULL, NULL) != SQLITE_OK ||
        write_stations(db, &metro->stations) || write_lines(db, &metro->lines) || write_edges(db, &metro->edges) ||
        sqlite3_exec(db, "UPDATE stations SET transfer=(SELECT count(DISTINCT line_id)>1 FROM edges "
                         "WHERE from_id=stations.id OR to_id=stations.id); COMMIT", NULL, NULL, NULL) != SQLITE_OK)
        goto done;
    result = 0;
done:
    if (db && result)
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
    sqlite3_close(db);
    dispose(&before);
    return result;
}
