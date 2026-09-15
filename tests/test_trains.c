#include "../src/ui/tui.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); failures++; } } while (0)

static void test_fixture(MapDb *db) {
    Trains t;
    int result = trains_init(&t, db);
    CHECK(result == 0);
    if (result) return;
    CHECK(t.line_count == db->metro.lines.rows.size);
    unsigned char seen[MAP_LIMIT] = {0};
    for (size_t i = 0; i < t.line_count; i++) {
        TrainTrack *track = &t.lines[i];
        CHECK(track->count > 0);
        for (size_t j = 0; j < track->count; j++) {
            TrainStep step = track->steps[j];
            if (step.geometry < 0) continue;
            Edge e = t.geometry[step.geometry].edge;
            CHECK(e.line_id == db->metro.lines.rows.items[i].id);
            seen[e.id] = 1;
            TrainPosition p;
            double begin = j ? track->steps[j - 1].end : 0;
            CHECK(trains_position(&t, track, (begin + step.end) / 2, &p));
            CHECK(p.edge_id == e.id && isfinite(p.x) && isfinite(p.y));
            TrainStep next = track->steps[(j + 1) % track->count];
            if (next.geometry >= 0) {
                Edge n = t.geometry[next.geometry].edge;
                CHECK((step.reverse ? e.from_station_id : e.to_station_id) ==
                      (next.reverse ? n.to_station_id : n.from_station_id));
            }
        }
    }
    for (size_t i = 0; i < t.count; i++) CHECK(seen[t.geometry[i].edge.id]);
    Graph graph = {0};
    Route route;
    CHECK(!route_init(&route));
    CHECK(!graph_build(&graph, &db->metro));
    int a = station_find_by_name(&db->metro.stations, "体育西路")->id;
    int b = station_find_by_name(&db->metro.stations, "广州南站")->id;
    for (int reverse = 0; reverse < 2; reverse++) {
        CHECK(!router_find_route(&graph, &db->metro, reverse ? b : a, reverse ? a : b,
                                ROUTE_MIN_STATIONS, &route));
        CHECK(!trains_plan(&t, &route));
        CHECK(t.journey.count == route.edges_ids.size);
        CHECK(t.journey.duration == route.total_seconds);
        TrainPosition start, wrapped;
        CHECK(trains_position(&t, &t.journey, 0, &start));
        MapStation station = db->stations[reverse ? b : a];
        CHECK(hypot(start.x - station.x, start.y - station.y) < 2);
        CHECK(trains_position(&t, &t.journey, t.journey.duration, &wrapped));
        CHECK(start.x == wrapped.x && start.y == wrapped.y);
        for (size_t i = 0; i < t.journey.count; i++) {
            double before = i ? t.journey.steps[i - 1].end : 0;
            TrainPosition p;
            CHECK(trains_position(&t, &t.journey, before, &p));
            CHECK(p.edge_id == route.edges_ids.items[i]);
        }
    }
    trains_advance(&t, 2, 0);
    CHECK(t.journey_time == 2 * TRAIN_SPEED_MULTIPLIER);
    t.paused = 1;
    trains_advance(&t, 10, 0);
    CHECK(t.journey_time == 2 * TRAIN_SPEED_MULTIPLIER);
    t.paused = 0;
    trains_advance(&t, 10, 1);
    CHECK(t.journey_time == 2 * TRAIN_SPEED_MULTIPLIER);
    CHECK(!trains_plan(&t, NULL));
    CHECK(!t.journey.count && t.journey_time == 0);
    CHECK(!router_find_route(&graph, &db->metro, a, a, ROUTE_MIN_STATIONS, &route));
    CHECK(!trains_plan(&t, &route));
    CHECK(!t.journey.count);
    graph_dispose(&graph);
    route_dispose(&route);
    trains_dispose(&t);
}

static void test_overlay(void) {
    MapSegment segment = {1000, 1000, 1100, 1000, 1};
    double end = 100;
    TrainGeometry geometry = {.edge = {.id = 1, .line_id = 1, .cost_time_second = 60},
                              .segments = &segment, .ends = &end, .length = 100, .count = 1};
    TrainStep step = {0, 0, 60};
    TrainTrack track = {&step, 1, 60};
    Trains t = {.geometry = &geometry, .count = 1, .journey = track, .journey_time = 30};
    MapFrame frame = {0};
    CHECK(!frame_resize(&frame, 80, 30));
    Viewport view = {1050, 1000, .5};
    int head = 15 * 80 + 40;
    trains_render(&t, 1, &frame, view, 80, 30);
    CHECK(frame.cells[head].glyph == 0x25b6);
    CHECK(frame.cells[head-1].glyph == 0x25b0 && frame.cells[head-2].glyph == 0x25b0);
    CHECK(frame.cells[head].color == 5);
    frame_clear(&frame);
    frame_text(&frame, 38, 15, 2, "站", 0, 0);
    trains_render(&t, 1, &frame, view, 80, 30);
    CHECK(frame.cells[head].glyph == '>');
    CHECK(frame.cells[head-2].glyph == 0x7ad9 && frame.cells[head-1].continuation);
    frame_glyph(&frame, 40, 15, MAP_TRANSFER_GLYPH, 0, 0);
    trains_render(&t, 1, &frame, view, 80, 30);
    CHECK(frame.cells[head].glyph == MAP_TRANSFER_GLYPH);
    frame_clear(&frame);
    view.scale = .1;
    trains_render(&t, 1, &frame, view, 80, 30);
    CHECK(frame.cells[head].glyph == '>' && !frame.cells[head-1].glyph);
    step.reverse = 1;
    frame_clear(&frame);
    trains_render(&t, 1, &frame, view, 80, 30);
    CHECK(frame.cells[head].glyph == '<');
    CHECK(unicode_width(0x25b0) == 1 && unicode_width(0x25b6) == 1);
    frame_dispose(&frame);
}
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    MapDb *db = calloc(1, sizeof(*db));
    if (!db || map_db_open(db, argv[1])) return 2;
    test_fixture(db);
    test_overlay();
    map_db_close(db);
    free(db);
    return failures ? 1 : 0;
}
