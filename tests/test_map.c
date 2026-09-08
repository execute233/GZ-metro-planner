#include "../src/ui/tui.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int failures;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                           \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)
static int station_id(MapDb *db, const char *name) {
    Station *s = station_find_by_name(&db->metro.stations, name);
    CHECK(s != NULL);
    return s ? s->id : 0;
}
static void test_unicode(void) {
    CHECK(unicode_width(0x4e2d) == 2);
    CHECK(unicode_width(0x28ff) == 1);
    CHECK(unicode_width(0x2500) == 1);
    const char *s = "体育A";
    uint32_t cp;
    CHECK(utf8_next(&s, &cp) && cp == 0x4f53);
    CHECK(utf8_next(&s, &cp) && cp == 0x80b2);
    CHECK(utf8_next(&s, &cp) && cp == 'A');
    CHECK(!utf8_next(&s, &cp));
    s = "\xe4";
    CHECK(utf8_next(&s, &cp) && cp == 0xfffd);
    CHECK(!utf8_next(&s, &cp));
    MapFrame f = {0};
    CHECK(!frame_resize(&f, 6, 2));
    frame_text(&f, 5, 0, 1, "站", 0, 0);
    CHECK(!f.cells[5].glyph);
    frame_text(&f, 1, 0, 4, "体育", 0, 0);
    CHECK(f.cells[2].continuation);
    CHECK(f.cells[4].continuation);
    frame_glyph(&f, 2, 0, 'A', 0, 0);
    CHECK(!f.cells[1].glyph);
    CHECK(f.cells[2].glyph == 'A');
    frame_glyph(&f, 2, 0, 0x4e2d, 0, 0);
    CHECK(!f.cells[4].continuation);
    frame_dispose(&f);
}
static void test_malformed(void) {
    MapSegments s = {0};
    unsigned char bad[] = {0x1a, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    CHECK(mvt_decode(bad, sizeof(bad), 0, 0, 0, &s) == -1);
    CHECK(!s.items);
    CHECK(mvt_decode(bad, 1, 0, 0, 0, &s) == -1);
    CHECK(mvt_decode(bad, 1, 20, 0, 0, &s) == -1);
    /* Produced independently with mapbox-vector-tile 2.2.0: edge 7, diagonal. */
    const unsigned char golden[] = {0x1a, 0x1c, 0x0a, 0x05, 0x65, 0x64, 0x67, 0x65, 0x73, 0x12,
                                    0x0e, 0x08, 0x07, 0x18, 0x02, 0x22, 0x08, 0x09, 0x00, 0x00,
                                    0x0a, 0x80, 0x40, 0x80, 0x40, 0x28, 0x80, 0x20, 0x78, 0x02};
    CHECK(!mvt_decode(golden, sizeof(golden), 1, 1, 0, &s));
    CHECK(s.count == 1);
    if (s.count == 1) {
        CHECK(s.items[0].edge_id == 7);
        CHECK(s.items[0].x0 == 2048 && s.items[0].y0 == 0);
        CHECK(s.items[0].x1 == 4096 && s.items[0].y1 == 2048);
    }
    for (size_t n = 1; n < sizeof(golden); n++)
        CHECK(mvt_decode(golden, n, 0, 0, 0, &s) == -1);
    mvt_dispose(&s);
    /* Deterministic bounded parser fuzz, plus every truncation of a valid MVT. */
    unsigned seed = 12345;
    unsigned char bytes[100];
    for (int n = 0; n < 1000; n++) {
        for (int i = 0; i < 100; i++) {
            seed = seed * 1664525u + 1013904223u;
            bytes[i] = (unsigned char)(seed >> 24);
        }
        (void)mvt_decode(bytes, (size_t)(n % 100 + 1), 0, 0, 0, &s);
        mvt_dispose(&s);
    }
}
static void test_bad_gzip(void) {
    MapDb *db = calloc(1, sizeof(*db));
    CHECK(db != NULL);
    if (!db)
        return;
    CHECK(sqlite3_open(":memory:", &db->db) == SQLITE_OK);
    CHECK(sqlite3_exec(db->db,
                       "CREATE TABLE tiles(zoom_level,tile_column,tile_row,tile_data);"
                       "INSERT INTO tiles VALUES(0,0,0,x'1f8b080000');",
                       NULL, NULL, NULL) == SQLITE_OK);
    CHECK(sqlite3_prepare_v2(
              db->db,
              "SELECT tile_data FROM tiles WHERE zoom_level=? AND tile_column=? AND tile_row=?", -1,
              &db->tile_query, NULL) == SQLITE_OK);
    const MapSegments *out;
    CHECK(map_db_tile(db, 0, 0, 0, &out) == -1);
    CHECK(out && out->count == 0);
    CHECK(map_db_tile(db, 0, 0, 0, &out) == -1);
    map_db_close(db);
    free(db);
}
static void test_tiles(MapDb *db) {
    sqlite3_stmt *q = NULL;
    CHECK(sqlite3_prepare_v2(db->db, "SELECT zoom_level,tile_column,tile_row FROM tiles", -1, &q,
                             NULL) == SQLITE_OK);
    size_t count = 0;
    int seen[MAP_LIMIT * 4] = {0};
    while (sqlite3_step(q) == SQLITE_ROW) {
        int z = sqlite3_column_int(q, 0), x = sqlite3_column_int(q, 1),
            y = (1 << z) - 1 - sqlite3_column_int(q, 2);
        const MapSegments *s;
        CHECK(map_db_tile(db, z, x, y, &s) == 0);
        CHECK(s && s->count);
        if (s)
            for (size_t i = 0; i < s->count; i++) {
                MapSegment v = s->items[i];
                CHECK(v.edge_id > 0 && v.edge_id < MAP_LIMIT * 4);
                CHECK(edge_find_by_id(&db->metro.edges, v.edge_id) != NULL);
                CHECK(isfinite(v.x0) && isfinite(v.y0) && v.x0 >= 0 && v.y0 >= 0 && v.x0 <= 4096 &&
                      v.y0 <= 4096);
                if (z == 0 && v.edge_id > 0 && v.edge_id < MAP_LIMIT * 4)
                    seen[v.edge_id] = 1;
            }
        const MapSegments *again;
        CHECK(!map_db_tile(db, z, x, y, &again));
        CHECK(s == again);
        count++;
    }
    sqlite3_finalize(q);
    CHECK(count > 100);
    for (size_t i = 0; i < db->metro.edges.rows.size; i++)
        CHECK(seen[db->metro.edges.rows.items[i].id]);
    const MapSegments *s;
    CHECK(map_db_tile(db, 5, 0, 0, &s) == 1);
    CHECK(map_db_tile(db, 5, -1, 0, &s) == -1);
}
static void test_routes_frames(MapDb *db) {
    TuiState s;
    CHECK(!tui_init(&s, db, 140, 48));
    const char *queries[] = {"体育西路", "tiyuxilu", "TYXL"};
    int from = station_id(db, "体育西路"), to = station_id(db, "广州南站");
    for (int i = 0; i < 3; i++) {
        snprintf(s.query, sizeof(s.query), "%s", queries[i]);
        tui_search(&s);
        CHECK(s.match_count == 1);
        CHECK(s.matches[0] == from);
    }
    strcpy(s.query, "nothinghere");
    tui_search(&s);
    CHECK(s.match_count == 0);
    s.from = from;
    s.to = to;
    for (int metric = 0; metric < 3; metric++) {
        s.metric = metric;
        tui_plan(&s, 140, 48);
        CHECK(s.ready);
        CHECK(s.route.total_stations > 1);
        CHECK(s.route.total_meters == (s.route.total_stations - 1) * 1000);
        CHECK(s.route.total_seconds == (s.route.total_stations - 1) * 60);
    }
    s.from = to;
    s.to = from;
    tui_plan(&s, 140, 48);
    CHECK(s.ready);
    CHECK(s.route.stations.items[0] == to);
    s.from = s.to;
    tui_plan(&s, 140, 48);
    CHECK(s.ready && s.route.total_stations == 1 && s.route.edges_ids.size == 0);
    s.from = station_id(db, "佛山大学");
    s.to = from;
    tui_plan(&s, 140, 48);
    CHECK(!s.ready);
    s.from = from;
    s.to = to;
    tui_plan(&s, 140, 48);
    const int sizes[][2] = {{140, 48}, {120, 40}, {90, 30}, {70, 24}, {50, 16}, {20, 8}};
    MapFrame f = {0};
    for (int j = 0; j < 6; j++)
        for (int focus = 0; focus < 4; focus++) {
            CHECK(!frame_resize(&f, sizes[j][0], sizes[j][1]));
            s.focus = focus;
            tui_frame(&s, &f);
            for (int i = 0; i < f.width * f.height; i++)
                if (f.cells[i].continuation)
                    CHECK(i % f.width > 0 && unicode_width(f.cells[i - 1].glyph) == 2);
        }
    double wx = s.view.x + (20 - 100) / s.view.scale, wy = s.view.y + (30 - 80) / s.view.scale;
    viewport_zoom(&s.view, 1.5, 100, 40, 20, 30);
    CHECK(fabs(s.view.x + (20 - 100) / s.view.scale - wx) < 1e-8);
    CHECK(fabs(s.view.y + (30 - 80) / s.view.scale - wy) < 1e-8);
    frame_dispose(&f);
    tui_dispose(&s);
}
int main(int argc, char **argv) {
    if (argc != 2)
        return 2;
    test_unicode();
    test_malformed();
    test_bad_gzip();
    MapDb *db = calloc(1, sizeof(*db));
    if (!db)
        return 2;
    if (map_db_open(db, argv[1])) {
        fprintf(stderr, "%s\n", db->error);
        free(db);
        return 1;
    }
    CHECK(db->metro.stations.rows.size == 357);
    CHECK(db->metro.edges.rows.size == 421);
    test_tiles(db);
    test_routes_frames(db);
    map_db_close(db);
    CHECK(map_db_open(db, "this-file-does-not-exist.mbtiles") == -1);
    CHECK(!db->db);
    free(db);
    printf("%d map checks failed\n", failures);
    return failures ? 1 : 0;
}
