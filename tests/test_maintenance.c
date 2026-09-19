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
    state->edit.action = atoi(action);
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
    start(state, "2");
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

static void keep_field(TuiState *state) {
    CHECK(tui_maintenance_submit(state, TEST_DB, 120, 40) == 0);
}

static void test_updates(TuiState *state) {
    Metro before = {0};
    CHECK(metro_io_load(TEST_DB, &before) == 0);
    Edge edge = before.edges.rows.items[0];
    Station original = *station_find_by_id(&before.stations, edge.from_station_id);
    const Station *neighbor = station_find_by_id(&before.stations, edge.to_station_id);
    const Line *original_line = line_find_by_id(&before.lines, edge.line_id);
    char coords[128];

    start(state, "5");
    CHECK(submit(state, "不存在的站点") == -1);
    CHECK(!state->edit.station.id);
    CHECK(submit(state, original.name) == 0);
    CHECK(!strcmp(state->edit.input, original.name));
    CHECK(submit(state, neighbor->name) == -1);
    CHECK(submit(state, "") == -1);
    CHECK(submit(state, "改名测试站") == 0);
    CHECK(!strcmp(state->edit.input, original.pinyin_name));
    CHECK(submit(state, "gaimingceshi") == 0);
    CHECK(submit(state, "gmcs") == 0);
    CHECK(submit(state, "Renamed station") == 0);
    CHECK(submit(state, "NaN,100") == -1);
    CHECK(submit(state, "4097,100") == -1);
    snprintf(coords, sizeof(coords), "%.17g,%.17g", neighbor->x, neighbor->y);
    CHECK(submit(state, coords) == -1);
    CHECK(submit(state, "3001,3002") == 0);
    CHECK(state->edit.confirm && !*state->edit.input);
    CHECK(station_find_by_name(&state->map->metro.stations, original.name) != NULL);
    execute("CREATE TRIGGER reject_update BEFORE INSERT ON stations BEGIN SELECT RAISE(ABORT,'test'); END");
    CHECK(submit(state, "y") == -1);
    CHECK(state->edit.confirm && state->edit.active);
    Metro unchanged = {0};
    CHECK(metro_io_load(TEST_DB, &unchanged) == 0);
    const Station *saved = station_find_by_id(&unchanged.stations, original.id);
    CHECK(saved && !strcmp(saved->name, original.name) && saved->x == original.x);
    dispose(&unchanged);
    execute("DROP TRIGGER reject_update");
    CHECK(submit(state, "y") == 1);
    saved = station_find_by_id(&state->map->metro.stations, original.id);
    CHECK(saved && !strcmp(saved->name, "改名测试站") && saved->x == 3001 && saved->y == 3002);
    CHECK(saved && !strcmp(saved->en_name, "Renamed station") && saved->transfer == original.transfer);
    CHECK(state->map->stations[original.id].x == 3001);
    strcpy(state->query, "gmcs");
    tui_search(state);
    CHECK(state->match_count == 1 && state->matches[0] == original.id);
    const MapSegments *segments = NULL;
    CHECK(map_db_tile(state->map, 0, 0, 0, &segments) == 0);
    int found = 0;
    if (segments) {
        for (size_t i = 0; i < segments->count; i++) {
            const MapSegment *s = &segments->items[i];
            if (s->edge_id == edge.id) {
                CHECK((s->x0 == 3001 && s->y0 == 3002) || (s->x1 == 3001 && s->y1 == 3002));
                found++;
            }
        }
    }
    CHECK(found == 1);
    check_tiles(state->map);

    start(state, "5");
    CHECK(submit(state, "改名测试站") == 0);
    for (int i = 0; i < 5; i++) keep_field(state);
    CHECK(state->edit.confirm && state->edit.station.x == 3001);
    CHECK(submit(state, "n") == 0);
    CHECK(!state->edit.action && state->edit.active);

    start(state, "6");
    CHECK(submit(state, "不存在的线路") == -1);
    CHECK(submit(state, original_line->name) == 0);
    CHECK(!strcmp(state->edit.input, original_line->name));
    CHECK(!state->edit.line.station_ids.items);
    const Line *other = &before.lines.rows.items[original_line == &before.lines.rows.items[0] ? 1 : 0];
    CHECK(submit(state, other->name) == -1);
    CHECK(submit(state, "改名测试线") == 0);
    CHECK(submit(state, "invalid") == -1);
    CHECK(submit(state, "12ABCD") == 0);
    char long_name[LINE_EN_MAX + 1];
    memset(long_name, 'a', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = 0;
    CHECK(submit(state, long_name) == -1);
    CHECK(submit(state, "Renamed line") == 0);
    CHECK(submit(state, "y") == 1);
    const Line *line = line_find_by_id(&state->map->metro.lines, original_line->id);
    CHECK(line && !strcmp(line->name, "改名测试线") && !strcmp(line->en_name, "Renamed line"));
    CHECK(line && line->rgb == 0x12abcd && line->color == original_line->color);
    CHECK(line && line->station_ids.size == original_line->station_ids.size);
    if (line && line->station_ids.size == original_line->station_ids.size)
        for (size_t i = 0; i < line->station_ids.size; i++)
            CHECK(line->station_ids.items[i] == original_line->station_ids.items[i]);
    CHECK(state->map->colors[original_line->id] == 0x12abcd);

    start(state, "6");
    CHECK(submit(state, "改名测试线") == 0);
    keep_field(state);
    keep_field(state);
    CHECK(submit(state, "") == 0);
    CHECK(submit(state, "n") == 0);
    line = line_find_by_id(&state->map->metro.lines, original_line->id);
    CHECK(line && !strcmp(line->en_name, "Renamed line"));
    start(state, "6");
    CHECK(submit(state, "改名测试线") == 0);
    keep_field(state);
    keep_field(state);
    CHECK(submit(state, "") == 0);
    CHECK(submit(state, "y") == 1);
    line = line_find_by_id(&state->map->metro.lines, original_line->id);
    CHECK(line && !*line->en_name);
    CHECK(state->map->metro.edges.rows.size == before.edges.rows.size);
    for (size_t i = 0; i < before.edges.rows.size; i++) {
        const Edge *a = &before.edges.rows.items[i];
        const Edge *b = edge_find_by_id(&state->map->metro.edges, a->id);
        CHECK(b && a->line_id == b->line_id && a->from_station_id == b->from_station_id &&
              a->to_station_id == b->to_station_id && a->cost_meters == b->cost_meters &&
              a->cost_time_second == b->cost_time_second);
    }
    state->from = edge.from_station_id;
    state->to = edge.to_station_id;
    tui_plan(state, 120, 40);
    CHECK(state->ready);
    dispose(&before);
}

static void test_add_interval(TuiState *state) {
    Edge original = state->map->metro.edges.rows.items[0];
    char line_name[LINE_NAME_MAX], from_name[STATION_NAME_MAX], to_name[STATION_NAME_MAX];
    strcpy(line_name, line_find_by_id(&state->map->metro.lines, original.line_id)->name);
    strcpy(from_name, station_find_by_id(&state->map->metro.stations, original.from_station_id)->name);
    strcpy(to_name, station_find_by_id(&state->map->metro.stations, original.to_station_id)->name);
    size_t edge_count = state->map->metro.edges.rows.size;
    size_t line_count = state->map->metro.lines.rows.size;

    for (int reverse = 0; reverse < 2; reverse++) {
        start(state, "7");
        CHECK(submit(state, "不存在的线路") == -1);
        CHECK(state->edit.step == 0);
        CHECK(submit(state, line_name) == 0);
        CHECK(submit(state, "不存在的站点") == -1);
        CHECK(submit(state, reverse ? to_name : from_name) == 0);
        CHECK(submit(state, reverse ? to_name : from_name) == -1);
        CHECK(submit(state, reverse ? from_name : to_name) == -1);
        CHECK(state->edit.step == 2 && state->edit.line.station_ids.size == 1);
        maintenance_close(&state->edit);
    }

    start(state, "1");
    CHECK(submit(state, "区间测试站") == 0);
    CHECK(submit(state, "qujianceshizhan") == 0);
    CHECK(submit(state, "qjcsz") == 0);
    CHECK(submit(state, "") == 0);
    CHECK(submit(state, "3100,3200") == 0);
    CHECK(submit(state, "y") == 1);
    int new_id = station_find_by_name(&state->map->metro.stations, "区间测试站")->id;
    state->from = original.from_station_id;
    state->to = new_id;
    tui_plan(state, 120, 40);
    CHECK(!state->ready);

    for (int cancel = 1; cancel >= 0; cancel--) {
        start(state, "7");
        CHECK(submit(state, line_name) == 0);
        CHECK(submit(state, from_name) == 0);
        Station *new_station = station_find_by_id(&state->map->metro.stations, new_id);
        Station *from = station_find_by_id(&state->map->metro.stations, original.from_station_id);
        double x = new_station->x, y = new_station->y;
        new_station->x = from->x;
        new_station->y = from->y;
        CHECK(submit(state, "区间测试站") == -1);
        new_station->x = x;
        new_station->y = y;
        CHECK(submit(state, "区间测试站") == 0);
        const char *invalid[] = {"NaN,100", "-1,100", "1,0", "1.5,100", "1,100001", "1,100x"};
        for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
            CHECK(submit(state, invalid[i]) == -1);
        CHECK(submit(state, "75,1200") == 0);
        CHECK(state->edit.confirm && state->edit.edges.rows.size == 1);
        CHECK(state->map->metro.edges.rows.size == edge_count);

        if (cancel) {
            CHECK(submit(state, "n") == 0);
            CHECK(!state->edit.action && state->edit.active);
            continue;
        }
        execute("CREATE TRIGGER reject_interval BEFORE INSERT ON edges BEGIN SELECT RAISE(ABORT,'test'); END");
        CHECK(submit(state, "y") == -1);
        CHECK(state->edit.confirm && state->map->metro.edges.rows.size == edge_count);
        Metro disk = {0};
        CHECK(metro_io_load(TEST_DB, &disk) == 0);
        CHECK(disk.edges.rows.size == edge_count);
        dispose(&disk);
        execute("DROP TRIGGER reject_interval");
        CHECK(submit(state, "y") == 1);
    }
    CHECK(state->map->metro.lines.rows.size == line_count);
    CHECK(state->map->metro.edges.rows.size == edge_count + 1);
    const Line *line = line_find_by_id(&state->map->metro.lines, original.line_id);
    int member = 0;
    for (size_t i = 0; i < line->station_ids.size; i++)
        member |= line->station_ids.items[i] == new_id;
    CHECK(member);
    for (int reverse = 0; reverse < 2; reverse++) {
        state->from = reverse ? new_id : original.from_station_id;
        state->to = reverse ? original.from_station_id : new_id;
        tui_plan(state, 120, 40);
        CHECK(state->ready && state->route.total_seconds == 75 && state->route.total_meters == 1200);
    }
    check_tiles(state->map);
    const Line *other_line = &state->map->metro.lines.rows.items[0];
    if (other_line->id == original.line_id)
        other_line = &state->map->metro.lines.rows.items[1];
    int other_line_id = other_line->id;
    start(state, "7");
    CHECK(submit(state, other_line->name) == 0);
    CHECK(submit(state, "区间测试站") == 0);
    CHECK(submit(state, from_name) == 0);
    CHECK(submit(state, "0,100000") == 0);
    CHECK(submit(state, "y") == 1);
    CHECK(state->map->metro.edges.rows.size == edge_count + 2);
    CHECK(station_find_by_id(&state->map->metro.stations, new_id)->transfer);
    CHECK(state->map->stations[new_id].transfer);
    const Edge *last = &state->map->metro.edges.rows.items[state->map->metro.edges.rows.size - 1];
    CHECK(last->line_id == other_line_id && last->cost_time_second == 0 && last->cost_meters == 100000);
    const Edge *preserved = edge_find_by_id(&state->map->metro.edges, original.id);
    CHECK(preserved && preserved->line_id == original.line_id &&
          preserved->from_station_id == original.from_station_id &&
          preserved->to_station_id == original.to_station_id &&
          preserved->cost_time_second == original.cost_time_second &&
          preserved->cost_meters == original.cost_meters);
    check_tiles(state->map);
    /* The save boundary must also reject a duplicate, independent of the form. */
    start(state, "7");
    state->edit.line.id = original.line_id;
    Edge duplicate = {.from_station_id = new_id, .to_station_id = original.from_station_id,
                      .cost_time_second = 75, .cost_meters = 1200};
    CHECK(!al_edge_push(&state->edit.edges.rows, duplicate));
    state->edit.confirm = 1;
    CHECK(maintenance_save(&state->edit, TEST_DB) == -1);
    maintenance_close(&state->edit);
}

static void select_interval(TuiState *state, const char *action, const char *line,
                            const char *from, const char *to) {
    start(state, action);
    CHECK(submit(state, line) == 0);
    CHECK(submit(state, from) == 0);
    CHECK(submit(state, to) == 0);
}

static const Edge *find_interval(TuiState *state, int line_id, int from, int to) {
    for (size_t i = 0; i < state->map->metro.edges.rows.size; i++) {
        const Edge *edge = &state->map->metro.edges.rows.items[i];
        if (edge->line_id == line_id &&
            ((edge->from_station_id == from && edge->to_station_id == to) ||
             (edge->from_station_id == to && edge->to_station_id == from)))
            return edge;
    }
    return NULL;
}

static void test_edit_intervals(TuiState *state) {
    const char *names[] = {"插站A", "插站B", "插站X"};
    const char *coords[] = {"3400,3000", "3600,3000", "3500,3100"};
    int ids[3];
    for (int i = 0; i < 3; i++) {
        start(state, "1");
        CHECK(submit(state, names[i]) == 0);
        CHECK(submit(state, "chazhan") == 0);
        CHECK(submit(state, "cz") == 0);
        CHECK(submit(state, "") == 0);
        CHECK(submit(state, coords[i]) == 0);
        CHECK(submit(state, "y") == 1);
        ids[i] = station_find_by_name(&state->map->metro.stations, names[i])->id;
    }
    start(state, "3");
    CHECK(submit(state, "区间维护线") == 0);
    CHECK(submit(state, "123456") == 0);
    CHECK(submit(state, names[0]) == 0);
    CHECK(submit(state, names[1]) == 0);
    CHECK(submit(state, "") == 0);
    CHECK(submit(state, "60,600") == 0);
    CHECK(submit(state, "y") == 1);
    int line_id = line_find_by_name(&state->map->metro.lines, "区间维护线")->id;
    int original_id = find_interval(state, line_id, ids[0], ids[1])->id;
    size_t count = state->map->metro.edges.rows.size;

    select_interval(state, "8", "区间维护线", names[1], names[0]);
    CHECK(!strcmp(state->edit.input, "60,600"));
    CHECK(submit(state, "1,0") == -1);
    CHECK(submit(state, "120,900") == 0);
    CHECK(submit(state, "n") == 0);
    CHECK(find_interval(state, line_id, ids[0], ids[1])->cost_time_second == 60);
    select_interval(state, "8", "区间维护线", names[1], names[0]);
    CHECK(submit(state, "120,900") == 0);
    CHECK(submit(state, "y") == 1);
    const Edge *edge = find_interval(state, line_id, ids[0], ids[1]);
    CHECK(edge && edge->id == original_id && edge->cost_time_second == 120 && edge->cost_meters == 900);
    CHECK(edge && edge->from_station_id == ids[0] && edge->to_station_id == ids[1]);
    state->from = ids[0];
    state->to = ids[1];
    tui_plan(state, 120, 40);
    CHECK(state->ready && state->route.total_seconds == 120 && state->route.total_meters == 900);

    /* An existing A-X connection prevents splitting A-B through X. */
    select_interval(state, "7", "区间维护线", names[0], names[2]);
    CHECK(submit(state, "30,300") == 0);
    CHECK(submit(state, "y") == 1);
    select_interval(state, "10", "区间维护线", names[1], names[0]);
    CHECK(submit(state, "不存在的站") == -1);
    CHECK(submit(state, names[1]) == -1);
    CHECK(submit(state, names[2]) == -1);
    CHECK(state->edit.step == 3 && !state->edit.edges.rows.size);
    select_interval(state, "9", "区间维护线", names[2], names[0]);
    CHECK(state->edit.confirm);
    CHECK(submit(state, "n") == 0);
    CHECK(find_interval(state, line_id, ids[0], ids[2]) != NULL);
    select_interval(state, "9", "区间维护线", names[2], names[0]);
    CHECK(submit(state, "y") == 1);
    CHECK(!find_interval(state, line_id, ids[0], ids[2]));
    CHECK(station_find_by_id(&state->map->metro.stations, ids[2]) != NULL);
    state->from = ids[0];
    state->to = ids[2];
    tui_plan(state, 120, 40);
    CHECK(!state->ready);

    select_interval(state, "10", "区间维护线", names[1], names[0]);
    CHECK(submit(state, names[2]) == 0);
    CHECK(submit(state, "40,400") == 0);
    CHECK(submit(state, "50,500") == 0);
    CHECK(submit(state, "n") == 0);
    CHECK(find_interval(state, line_id, ids[0], ids[1]) != NULL);
    CHECK(state->map->metro.edges.rows.size == count);
    select_interval(state, "10", "区间维护线", names[1], names[0]);
    CHECK(submit(state, names[2]) == 0);
    CHECK(submit(state, "40,400") == 0);
    CHECK(submit(state, "NaN,500") == -1);
    CHECK(state->edit.edges.rows.size == 1 && !state->edit.confirm);
    CHECK(submit(state, "50,500") == 0);
    CHECK(state->edit.confirm);

    execute("CREATE TRIGGER reject_split BEFORE INSERT ON tiles BEGIN SELECT RAISE(ABORT,'test'); END");
    CHECK(submit(state, "y") == -1);
    CHECK(find_interval(state, line_id, ids[0], ids[1]) != NULL);
    Metro disk = {0};
    CHECK(metro_io_load(TEST_DB, &disk) == 0);
    CHECK(disk.edges.rows.size == count && edge_find_by_id(&disk.edges, original_id) != NULL);
    dispose(&disk);
    execute("DROP TRIGGER reject_split");
    CHECK(submit(state, "y") == 1);
    CHECK(!edge_find_by_id(&state->map->metro.edges, original_id));
    CHECK(state->map->metro.edges.rows.size == count + 1);
    const Edge *bx = find_interval(state, line_id, ids[1], ids[2]);
    const Edge *xa = find_interval(state, line_id, ids[2], ids[0]);
    CHECK(bx && bx->cost_time_second == 40 && bx->cost_meters == 400);
    CHECK(xa && xa->cost_time_second == 50 && xa->cost_meters == 500);
    for (int reverse = 0; reverse < 2; reverse++) {
        state->from = ids[reverse];
        state->to = ids[1 - reverse];
        tui_plan(state, 120, 40);
        CHECK(state->ready && state->route.total_seconds == 90 && state->route.total_meters == 900);
    }
    check_tiles(state->map);
    /* Parameter edits must not rewrite any tile geometry. */
    execute("CREATE TRIGGER reject_geometry BEFORE INSERT ON tiles BEGIN SELECT RAISE(ABORT,'test'); END");
    select_interval(state, "8", "区间维护线", names[2], names[1]);
    CHECK(submit(state, "45,450") == 0);
    CHECK(submit(state, "y") == 1);
    execute("DROP TRIGGER reject_geometry");

    /* Another line using the same endpoints survives deletion. */
    char other_name[LINE_NAME_MAX];
    strcpy(other_name, state->map->metro.lines.rows.items[0].name);
    int other_id = state->map->metro.lines.rows.items[0].id;
    select_interval(state, "7", other_name, names[1], names[2]);
    CHECK(submit(state, "80,800") == 0);
    CHECK(submit(state, "y") == 1);
    CHECK(station_find_by_id(&state->map->metro.stations, ids[1])->transfer);
    select_interval(state, "9", "区间维护线", names[1], names[2]);
    execute("CREATE TRIGGER reject_delete BEFORE INSERT ON tiles BEGIN SELECT RAISE(ABORT,'test'); END");
    CHECK(submit(state, "y") == -1);
    CHECK(find_interval(state, line_id, ids[1], ids[2]) != NULL);
    execute("DROP TRIGGER reject_delete");
    CHECK(submit(state, "y") == 1);
    CHECK(!find_interval(state, line_id, ids[1], ids[2]));
    CHECK(find_interval(state, other_id, ids[1], ids[2]) != NULL);
    CHECK(!station_find_by_id(&state->map->metro.stations, ids[1])->transfer);
    CHECK(line_find_by_id(&state->map->metro.lines, line_id) != NULL);
    check_tiles(state->map);

    start(state, "8");
    CHECK(submit(state, "区间维护线") == 0);
    CHECK(submit(state, names[0]) == 0);
    CHECK(submit(state, names[1]) == -1); /* Connected by a path, but not a direct edge. */
    CHECK(state->edit.step == 2);
    maintenance_close(&state->edit);

    select_interval(state, "8", "区间维护线", names[2], names[0]);
    CHECK(submit(state, "55,550") == 0);
    char sql[128];
    int changed_id = state->edit.original_edge.id;
    snprintf(sql, sizeof(sql), "UPDATE edges SET seconds=99 WHERE id=%d", changed_id);
    execute(sql);
    CHECK(submit(state, "y") == -1);
    CHECK(strstr(state->edit.message, "原区间已变化") != NULL);
    CHECK(metro_io_load(TEST_DB, &disk) == 0);
    CHECK(edge_find_by_id(&disk.edges, changed_id)->cost_time_second == 99);
    dispose(&disk);
    maintenance_close(&state->edit);
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
    test_station(&state);
    test_line(&state);
    test_rollback(&state);
    test_updates(&state);
    CHECK(state.map->metro.stations.rows.size == 357);
    CHECK(state.map->metro.lines.rows.size == 21);
    CHECK(state.map->metro.edges.rows.size == 421);
    test_add_interval(&state);
    test_edit_intervals(&state);
    tui_dispose(&state);
    map_db_close(db);
    free(db);
    test_invalid_database();
    test_empty_rebuild(argv[1]);
    remove(TEST_DB);
    printf("%d maintenance checks failed\n", failures);
    return failures ? 1 : 0;
}
