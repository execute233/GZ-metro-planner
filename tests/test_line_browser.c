#include "../src/ui/tui.h"
#include "../src/algo/line_paths.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); failures++; } } while (0)

/* Check actual connectivity and exact edge coverage, independent of run order. */
static void check_paths(const Metro *metro, int line_id, const ArrayList_Int *paths) {
    int *seen = calloc(metro->edges.rows.size, sizeof(*seen));
    CHECK(seen != NULL);
    if (!seen)
        return;
    int previous = 0, run_size = 0;
    for (size_t i = 0; i < paths->size; i++) {
        int id = paths->items[i];
        if (!id) {
            CHECK(run_size >= 2);
            previous = run_size = 0;
            continue;
        }
        CHECK(station_find_by_id(&metro->stations, id));
        run_size++;
        if (previous) {
            int found = 0;
            for (size_t j = 0; j < metro->edges.rows.size; j++) {
                const Edge *e = &metro->edges.rows.items[j];
                if (e->line_id == line_id &&
                    ((e->from_station_id == previous && e->to_station_id == id) ||
                     (e->to_station_id == previous && e->from_station_id == id))) {
                    seen[j]++;
                    found++;
                }
            }
            CHECK(found == 1);
        }
        previous = id;
    }
    CHECK(!paths->size || run_size >= 2);
    for (size_t i = 0; i < metro->edges.rows.size; i++)
        CHECK(seen[i] == (metro->edges.rows.items[i].line_id == line_id));
    free(seen);
}

static int has_text(const MapFrame *f, const char *text) {
    for (int y = 0; y < f->height; y++) {
        for (int x = 0; x < f->width; x++) {
            const char *p = text;
            uint32_t cp;
            int column = x, match = 1;
            while (utf8_next(&p, &cp)) {
                if (column >= f->width || f->cells[y * f->width + column].glyph != cp) {
                    match = 0;
                    break;
                }
                column += unicode_width(cp);
            }
            if (match)
                return 1;
        }
    }
    return 0;
}

static void draw(LineBrowser *b, const Metro *m, MapFrame *f) {
    frame_clear(f);
    line_browser_frame(b, m, f);
}

static void test_topology_and_ui(void) {
    Metro m = {0};
    for (int i = 1; i <= 9; i++) {
        Station s = {.id = i * 10};
        snprintf(s.name, sizeof(s.name), "中文站%d", i);
        CHECK(!al_station_push(&m.stations.rows, s));
    }
    Line line = {.id = 1, .name = "测试线"};
    CHECK(!al_line_push(&m.lines.rows, line));
    line = (Line){.id = 2, .name = "换乘线"};
    CHECK(!al_line_push(&m.lines.rows, line));
    line = (Line){.id = 3, .name = "空线路"};
    CHECK(!al_line_push(&m.lines.rows, line));
    /* Shuffled/reversed edges: three branches, a disconnected cycle and a pair. */
    const int pairs[][3] = {{20, 30, 1}, {60, 70, 1}, {40, 20, 1}, {10, 20, 1},
                           {50, 70, 1}, {60, 50, 1}, {90, 80, 1},
                           {20, 80, 2}, {20, 90, 2}};
    for (size_t i = 0; i < sizeof(pairs) / sizeof(pairs[0]); i++) {
        Edge e = {.id = (int)i + 1, .from_station_id = pairs[i][0],
                  .to_station_id = pairs[i][1], .line_id = pairs[i][2]};
        CHECK(!al_edge_push(&m.edges.rows, e));
    }
    ArrayList_Int paths = {0};
    CHECK(!line_paths(&m, 1, &paths));
    check_paths(&m, 1, &paths);
    int groups = 1, cycles = 0, first = paths.items[0];
    for (size_t i = 1; i < paths.size; i++) {
        if (!paths.items[i]) {
            groups++;
            first = paths.items[i + 1];
        } else if (paths.items[i - 1] && paths.items[i] == first)
            cycles++;
    }
    CHECK(groups == 5 && cycles == 1);
    al_int_dispose(&paths);
    CHECK(!line_paths(&m, 3, &paths) && !paths.size);
    CHECK(!line_paths(&m, 999, &paths) && !paths.size);
    al_int_dispose(&paths);

    MapFrame f = {0};
    LineBrowser b = {.active = 1};
    CHECK(!frame_resize(&f, 100, 50));
    draw(&b, &m, &f);
    CHECK(has_text(&f, "线路目录") && has_text(&f, "空线路"));
    line_browser_key(&b, &m, LINE_ENTER, f.height);
    draw(&b, &m, &f);
    CHECK(has_text(&f, "中文站2") && has_text(&f, "换乘：换乘线"));
    CHECK(!has_text(&f, "换乘：测试线"));
    CHECK(has_text(&f, "回到起始站，闭合"));
    CHECK(has_text(&f, "站段 5"));
    CHECK(!frame_resize(&f, 50, 16));
    memset(m.lines.rows.items[1].name, 'A', 55);
    strcpy(m.lines.rows.items[1].name + 55, "末尾");
    draw(&b, &m, &f);
    CHECK(has_text(&f, "末尾"));
    strcpy(m.lines.rows.items[1].name, "换乘线");
    draw(&b, &m, &f);
    for (int i = 0; i < 30; i++)
        line_browser_key(&b, &m, LINE_PAGE_DOWN, f.height);
    draw(&b, &m, &f);
    CHECK(b.scroll == b.row_count - 10);
    CHECK(has_text(&f, "中文站8"));
    CHECK(!frame_resize(&f, 100, 50));
    draw(&b, &m, &f);
    CHECK(b.scroll == 0 && has_text(&f, "站段 1"));
    line_browser_key(&b, &m, LINE_BACK, f.height);
    CHECK(b.active && !b.detail && !b.paths.items);
    line_browser_key(&b, &m, LINE_PAGE_DOWN, f.height);
    CHECK(b.selected == 2);
    line_browser_key(&b, &m, LINE_ENTER, f.height);
    draw(&b, &m, &f);
    CHECK(has_text(&f, "此线路暂无区间"));
    line_browser_key(&b, &m, LINE_BACK, f.height);
    line_browser_key(&b, &m, LINE_BACK, f.height);
    CHECK(!b.active && !b.paths.items);
    b.active = 1;
    size_t size = m.lines.rows.size;
    m.lines.rows.size = 0;
    line_browser_key(&b, &m, LINE_ENTER, f.height);
    draw(&b, &m, &f);
    CHECK(!b.detail && has_text(&f, "图集暂无线路"));
    m.lines.rows.size = size;
    line_browser_close(&b);
    frame_dispose(&f);
    station_table_dispose(&m.stations);
    line_table_dispose(&m.lines);
    edge_table_dispose(&m.edges);
}

static void dump_frame(const MapFrame *f, const char *path) {
    FILE *out = fopen(path, "w");
    CHECK(out != NULL);
    if (!out)
        return;
    fprintf(out, "%d %d\n", f->width, f->height);
    for (int i = 0; i < f->width * f->height; i++)
        fprintf(out, "%u %d %d\n", f->cells[i].glyph, f->cells[i].color, f->cells[i].continuation);
    CHECK(fclose(out) == 0);
}

int main(int argc, char **argv) {
    if (argc < 2)
        return 2;
    test_topology_and_ui();
    MapDb *db = calloc(1, sizeof(*db));
    CHECK(db && !map_db_open(db, argv[1]));
    if (!db || !db->db)
        return 1;
    TuiState s;
    CHECK(!tui_init(&s, db, 90, 30));
    s.from = station_find_by_name(&db->metro.stations, "体育西路")->id;
    s.to = station_find_by_name(&db->metro.stations, "广州南站")->id;
    tui_plan(&s, 90, 30);
    CHECK(s.ready);
    Viewport view = s.view;
    int *route = s.route.stations.items;
    s.scroll = 2;
    s.browser.active = 1;
    MapFrame f = {0};
    CHECK(!frame_resize(&f, 90, 30));
    tui_frame(&s, &f);
    if (argc == 3)
        dump_frame(&f, "line-directory.frame");
    CHECK(!frame_resize(&f, 50, 16));
    tui_frame(&s, &f);
    if (argc == 3)
        dump_frame(&f, "line-directory-narrow.frame");
    for (size_t i = 0; i < db->metro.lines.rows.size; i++)
        line_browser_key(&s.browser, &db->metro, LINE_DOWN, f.height);
    tui_frame(&s, &f);
    CHECK(s.browser.selected == (int)db->metro.lines.rows.size - 1);
    CHECK(has_text(&f, db->metro.lines.rows.items[s.browser.selected].name));
    CHECK(!frame_resize(&f, 90, 30));
    for (size_t i = 0; i < db->metro.lines.rows.size; i++) {
        s.browser.selected = (int)i;
        line_browser_key(&s.browser, &db->metro, LINE_ENTER, f.height);
        check_paths(&db->metro, db->metro.lines.rows.items[i].id, &s.browser.paths);
        tui_frame(&s, &f);
        CHECK(s.browser.row_count > 0);
        if (argc == 3 && i == 2) {
            dump_frame(&f, "line-detail.frame");
            CHECK(!frame_resize(&f, 50, 16));
            tui_frame(&s, &f);
            dump_frame(&f, "line-detail-narrow.frame");
            CHECK(!frame_resize(&f, 90, 30));
        }
        line_browser_key(&s.browser, &db->metro, LINE_BACK, f.height);
    }
    line_browser_key(&s.browser, &db->metro, LINE_BACK, f.height);
    CHECK(s.ready && s.route.stations.items == route && s.scroll == 2);
    CHECK(s.view.x == view.x && s.view.y == view.y && s.view.scale == view.scale);
    CHECK(!s.browser.active);
    tui_frame(&s, &f);
    CHECK(has_text(&f, "L 线路"));
    frame_dispose(&f);
    tui_dispose(&s);
    map_db_close(db);
    free(db);
    printf("%d line browser checks failed\n", failures);
    return failures ? 1 : 0;
}
