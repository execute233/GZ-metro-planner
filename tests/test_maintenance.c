#include "../src/ui/tui.h"
#include "../src/io/metro_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; \
} } while (0)
#define TEST_DB "maintenance_test.mbtiles"

static void dispose(Metro *metro) {
    station_table_dispose(&metro->stations);
    line_table_dispose(&metro->lines);
    edge_table_dispose(&metro->edges);
}

static void execute(const char *sql) {
    sqlite3 *db = NULL;
    CHECK(sqlite3_open(TEST_DB, &db) == SQLITE_OK);
    CHECK(sqlite3_exec(db, sql, NULL, NULL, NULL) == SQLITE_OK);
    CHECK(sqlite3_close(db) == SQLITE_OK);
}

static int copy_database(const char *source) {
    FILE *in = fopen(source, "rb"), *out = fopen(TEST_DB, "wb");
    if (!in || !out) {
        if (in) fclose(in);
        if (out) fclose(out);
        return -1;
    }
    unsigned char bytes[4096];
    size_t size;
    int result = 0;
    while ((size = fread(bytes, 1, sizeof(bytes), in)))
        if (fwrite(bytes, 1, size, out) != size)
            result = -1;
    if (ferror(in)) result = -1;
    fclose(in);
    if (fclose(out)) result = -1;
    return result;
}

static int submit(TuiState *state, const char *text) {
    snprintf(state->edit.input, sizeof(state->edit.input), "%s", text);
    return tui_maintenance_submit(state, TEST_DB, 120, 40);
}

static void start(TuiState *state, const char *action) {
    maintenance_close(&state->edit);
    state->edit.active = 1;
    CHECK(submit(state, action) == 0);
}

static void check_tiles(MapDb *db) {
    sqlite3_stmt *q = NULL;
    CHECK(sqlite3_prepare_v2(db->db, "SELECT zoom_level,tile_column,tile_row,tile_data FROM tiles",
                             -1, &q, NULL) == SQLITE_OK);
    int seen[METRO_EDGE_LIMIT] = {0};
    int rc;
    while ((rc = sqlite3_step(q)) == SQLITE_ROW) {
        const unsigned char *bytes = sqlite3_column_blob(q, 3);
        CHECK(sqlite3_column_bytes(q, 3) >= 2 && bytes[0] == 31 && bytes[1] == 139);
        int z = sqlite3_column_int(q, 0), x = sqlite3_column_int(q, 1);
        int y = (1 << z) - 1 - sqlite3_column_int(q, 2);
        const MapSegments *segments;
        CHECK(map_db_tile(db, z, x, y, &segments) == 0);
        if (!segments)
            continue;
        for (size_t i = 0; i < segments->count; i++) {
            int id = segments->items[i].edge_id;
            CHECK(edge_find_by_id(&db->metro.edges, id) != NULL);
            if (z == 0 && id > 0 && id < METRO_EDGE_LIMIT)
                seen[id] = 1;
        }
    }
    CHECK(rc == SQLITE_DONE);
    sqlite3_finalize(q);
    for (size_t i = 0; i < db->metro.edges.rows.size; i++)
        CHECK(seen[db->metro.edges.rows.items[i].id]);
}

static void test_station(TuiState *state) {
    start(state, "1");
    CHECK(submit(state, "测试新站") == 0);
    CHECK(submit(state, "ceshixinzhan") == 0);
    CHECK(submit(state, "csxz") == 0);
    CHECK(submit(state, "Test's station, SQL; intact") == 0);
    CHECK(submit(state, "NaN,100") == -1);
    CHECK(submit(state, "4097,100") == -1);
    CHECK(submit(state, "3000,3000junk") == -1);
    CHECK(submit(state, "3000,3000") == 0);
    CHECK(state->edit.confirm);
    CHECK(!station_find_by_name(&state->map->metro.stations, "测试新站"));
    CHECK(submit(state, "y") == 1);
    CHECK(!state->edit.active);
    Station *station = station_find_by_name(&state->map->metro.stations, "测试新站");
    CHECK(station && station->x == 3000 && station->y == 3000);
    CHECK(station && !strcmp(station->en_name, "Test's station, SQL; intact"));
    strcpy(state->query, "csxz");
    tui_search(state);
    CHECK(state->match_count == 1);
    start(state, "1");
    CHECK(submit(state, "测试新站") == -1);
    maintenance_close(&state->edit);
}

static void test_line(TuiState *state) {
    start(state, "3");
    CHECK(submit(state, "测试线路") == 0);
    CHECK(submit(state, "invalid") == -1);
    CHECK(submit(state, "FF8800") == 0);
    CHECK(submit(state, "测试新站") == 0);
    CHECK(submit(state, "测试新站") == -1);
    CHECK(submit(state, "不存在的站") == -1);
    CHECK(submit(state, "体育西路") == 0);
    CHECK(submit(state, "") == 0);
    CHECK(submit(state, "bad") == -1);
    CHECK(submit(state, "1,0") == -1);
    CHECK(submit(state, "1.2,1000") == -1);
    CHECK(submit(state, "75,1200") == 0);
    CHECK(submit(state, "y") == 1);
    Line *line = line_find_by_name(&state->map->metro.lines, "测试线路");
    CHECK(line && line->rgb == 0xff8800 && line->station_ids.size == 2);
    Station *from = station_find_by_name(&state->map->metro.stations, "测试新站");
    Station *to = station_find_by_name(&state->map->metro.stations, "体育西路");
    if (!from || !to || !line)
        return;
    CHECK(to->transfer);
    state->from = from->id;
    state->to = to->id;
    tui_plan(state, 120, 40);
    CHECK(state->ready && state->route.total_meters == 1200 && state->route.total_seconds == 75);
    const MapSegments *segments;
    CHECK(map_db_tile(state->map, 0, 0, 0, &segments) == 0);
    int seen = 0;
    for (size_t i = 0; i < segments->count; i++) {
        const Edge *edge = edge_find_by_id(&state->map->metro.edges, segments->items[i].edge_id);
        if (edge && edge->line_id == line->id)
            seen++;
    }
    CHECK(seen == 1);
    check_tiles(state->map);
    start(state, "2");
    CHECK(submit(state, "测试新站") == -1);
    maintenance_close(&state->edit);
}

static void test_rollback(TuiState *state) {
    start(state, "4");
    CHECK(submit(state, "测试线路") == 0);
    execute("CREATE TRIGGER reject_save BEFORE INSERT ON stations BEGIN SELECT RAISE(ABORT,'test'); END");
    CHECK(submit(state, "y") == -1);
    CHECK(line_find_by_name(&state->map->metro.lines, "测试线路") != NULL);
    MapDb *loaded = calloc(1, sizeof(*loaded));
    CHECK(loaded != NULL);
    if (loaded) {
        CHECK(map_db_open(loaded, TEST_DB) == 0);
        CHECK(line_find_by_name(&loaded->metro.lines, "测试线路") != NULL);
        check_tiles(loaded);
        const MapSegments *segments;
        CHECK(map_db_tile(loaded, 0, 0, 0, &segments) == 0);
        CHECK(segments && segments->count > 0);
        map_db_close(loaded);
        free(loaded);
    }
    execute("DROP TRIGGER reject_save");
    CHECK(submit(state, "y") == 1);
    CHECK(!line_find_by_name(&state->map->metro.lines, "测试线路"));
    check_tiles(state->map);
    CHECK(!state->ready && !state->from && !state->to);
    start(state, "2");
    CHECK(submit(state, "测试新站") == 0);
    CHECK(submit(state, "n") == 0);
    CHECK(station_find_by_name(&state->map->metro.stations, "测试新站") != NULL);
    CHECK(submit(state, "2") == 0);
    CHECK(submit(state, "测试新站") == 0);
    CHECK(submit(state, "y") == 1);
    CHECK(!station_find_by_name(&state->map->metro.stations, "测试新站"));
}

static void test_invalid_database(void) {
    Metro model = {0};
    CHECK(metro_io_load(TEST_DB, &model) == 0);
    size_t stations = model.stations.rows.size;
    CHECK(metro_io_load("missing-maintenance.mbtiles", &model) == -1);
    CHECK(model.stations.rows.size == stations);
    model.edges.rows.items[0].cost_meters = 0;
    CHECK(metro_io_save(TEST_DB, &model) == -1);
    CHECK(metro_io_load(TEST_DB, &model) == 0);
    CHECK(model.edges.rows.items[0].cost_meters > 0);
    CHECK(metro_io_save("missing-maintenance.mbtiles", &model) == -1);
    FILE *missing = fopen("missing-maintenance.mbtiles", "rb");
    CHECK(missing == NULL);
    if (missing) fclose(missing);
    execute("INSERT INTO edges SELECT 10000,line_id,to_id,from_id,seconds,meters FROM edges WHERE id=1");
    CHECK(metro_io_load(TEST_DB, &model) == -1);
    execute("DELETE FROM edges WHERE id=10000; UPDATE metadata SET value='1' WHERE name='gzmp_schema'");
    CHECK(metro_io_load(TEST_DB, &model) == -1);
    execute("UPDATE metadata SET value='2' WHERE name='gzmp_schema'");
    execute("UPDATE edges SET to_id=9999 WHERE id=1");
    CHECK(metro_io_load(TEST_DB, &model) == -1);
    CHECK(model.stations.rows.size == stations);
    execute("UPDATE edges SET to_id=2 WHERE id=1; UPDATE stations SET id=4294967297 WHERE id=1");
    CHECK(metro_io_load(TEST_DB, &model) == -1);
    dispose(&model);
}

static void test_empty_rebuild(const char *source) {
    CHECK(copy_database(source) == 0);
    Metro model = {0};
    CHECK(metro_io_save(TEST_DB, &model) == 0);
    MapDb *db = calloc(1, sizeof(*db));
    CHECK(db != NULL);
    if (!db) return;
    CHECK(map_db_open(db, TEST_DB) == 0);
    TuiState state;
    CHECK(tui_init(&state, db, 80, 24) == 0);
    CHECK(state.view.scale > 0);
    check_tiles(db);
    tui_dispose(&state);
    map_db_close(db);
    Station a = {.name = "新站A", .x = 100, .y = 100};
    Station b = {.name = "新站B", .x = 3000, .y = 3000};
    CHECK(station_add(&model.stations, &a) == 0);
    CHECK(station_add(&model.stations, &b) == 0);
    Line line = {.name = "新线", .color = 37, .rgb = 0x123456};
    CHECK(line_add(&model.lines, &line) == 0);
    Edge edge = {.line_id = line.id, .from_station_id = a.id, .to_station_id = b.id,
                 .cost_meters = 1000, .cost_time_second = 60};
    CHECK(edge_add(&model.edges, &edge) == 0);
    CHECK(metro_io_save(TEST_DB, &model) == 0);
    CHECK(map_db_open(db, TEST_DB) == 0);
    CHECK(db->maxzoom == 5);
    check_tiles(db);
    map_db_close(db);
    free(db);
    dispose(&model);
}

static void test_forms(TuiState *state) {
    MapFrame frame = {0};
    const int sizes[][2] = {{140, 48}, {90, 30}, {50, 16}};
    start(state, "1");
    CHECK(submit(state, "临时站") == 0);
    for (size_t j = 0; j < sizeof(sizes) / sizeof(sizes[0]); j++) {
        CHECK(!frame_resize(&frame, sizes[j][0], sizes[j][1]));
        tui_frame(state, &frame);
        for (int i = 0; i < frame.width * frame.height; i++)
            if (frame.cells[i].continuation)
                CHECK(i % frame.width > 0 && unicode_width(frame.cells[i - 1].glyph) == 2);
    }
    maintenance_close(&state->edit);
    CHECK(!station_find_by_name(&state->map->metro.stations, "临时站"));
    frame_dispose(&frame);
}

int main(int argc, char **argv) {
    if (argc != 2 || copy_database(argv[1]))
        return 2;
    MapDb *db = calloc(1, sizeof(*db));
    if (!db || map_db_open(db, TEST_DB)) {
        fprintf(stderr, "cannot open maintenance fixture\n");
        free(db);
        return 2;
    }
    TuiState state;
    CHECK(!tui_init(&state, db, 120, 40));
    test_forms(&state);
    test_station(&state);
    test_line(&state);
    test_rollback(&state);
    CHECK(state.map->metro.stations.rows.size == 357);
    CHECK(state.map->metro.lines.rows.size == 21);
    CHECK(state.map->metro.edges.rows.size == 421);
    tui_dispose(&state);
    map_db_close(db);
    free(db);
    test_invalid_database();
    test_empty_rebuild(argv[1]);
    remove(TEST_DB);
    printf("%d maintenance checks failed\n", failures);
    return failures ? 1 : 0;
}
