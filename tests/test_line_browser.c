#include "../src/ui/tui.h"
#include "../src/algo/line_paths.h"
#include "../src/render/line_diagram.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); failures++; } } while (0)

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

/* Connectivity plus exact edge coverage, independent of traversal order. */
static void check_paths(const Metro *m, int line_id, const ArrayList_Int *paths) {
    int *seen = calloc(m->edges.rows.size ? m->edges.rows.size : 1, sizeof(*seen));
    CHECK(seen != NULL);
    if (!seen)
        return;
    int previous = 0;
    for (size_t i = 0; i < paths->size; i++) {
        int id = paths->items[i];
        if (previous && id) {
            int found = 0;
            for (size_t j = 0; j < m->edges.rows.size; j++) {
                const Edge *e = &m->edges.rows.items[j];
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
    for (size_t i = 0; i < m->edges.rows.size; i++)
        CHECK(seen[i] == (m->edges.rows.items[i].line_id == line_id));
    free(seen);
}

static int is_marker(uint32_t cp) { return cp == 0x25cf || cp == 0x25c6; }

/* Every station must be visible exactly once across non-overlapping pages,
 * including junctions, closed rings and disconnected components. */
static void check_coverage(const Metro *m, int id, const ArrayList_Int *paths, int width) {
    int members[MAP_LIMIT] = {0}, expected = 0;
    for (size_t i = 0; i < paths->size; i++) {
        int station = paths->items[i];
        if (station && !members[station]) { members[station] = 1; expected++; }
    }
    MapFrame f = {0};
    CHECK(!frame_resize(&f, width, 30));
    int rows = line_diagram_render(&f, m, id, paths, width, 0);
    CHECK(rows >= 0);
    int found = 0;
    for (int scroll = 0; scroll < rows; scroll += 24) {
        frame_clear(&f);
        CHECK(line_diagram_render(&f, m, id, paths, width, scroll) == rows);
        for (int y = 3; y < 27; y++) {
            for (int x = 0; x < width; x++) {
                MapCell c = f.cells[y * width + x];
                found += is_marker(c.glyph);
                if (c.continuation)
                    CHECK(x > 0 && unicode_width(f.cells[y * width + x - 1].glyph) == 2);
            }
        }
    }
    CHECK(found == expected);
    frame_dispose(&f);
}

static void test_synthetic(void) {
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
    const int pairs[][3] = {{20,30,1},{60,70,1},{40,20,1},{10,20,1},
                           {50,70,1},{60,50,1},{90,80,1},{20,80,2},{20,90,2}};
    for (size_t i = 0; i < sizeof(pairs) / sizeof(pairs[0]); i++) {
        Edge e = {.id = (int)i + 1, .from_station_id = pairs[i][0],
                  .to_station_id = pairs[i][1], .line_id = pairs[i][2]};
        CHECK(!al_edge_push(&m.edges.rows, e));
    }
    ArrayList_Int paths = {0};
    CHECK(!line_paths(&m, 1, &paths));
    check_paths(&m, 1, &paths);
    check_coverage(&m, 1, &paths, 115);
    check_coverage(&m, 1, &paths, 33);
    /* A long transfer label on the ring must wrap without hiding its nodes. */
    Edge transfer = {.id = 10, .line_id = 2, .from_station_id = 50, .to_station_id = 20};
    CHECK(!al_edge_push(&m.edges.rows, transfer));
    memset(m.lines.rows.items[1].name, 'A', 55);
    strcpy(m.lines.rows.items[1].name + 55, "末尾");
    check_coverage(&m, 1, &paths, 33);
    strcpy(m.lines.rows.items[1].name, "换乘线");
    al_int_dispose(&paths);
    MapFrame f = {0};
    LineBrowser b = {.active = 1};
    CHECK(!frame_resize(&f, 80, 60));
    line_browser_frame(&b, &m, &f);
    CHECK(has_text(&f, "换乘 换乘线"));
    CHECK(!has_text(&f, "换乘 测试线"));
    line_browser_key(&b, &m, LINE_PAGE_DOWN, f.height);
    CHECK(b.selected == 2 && !b.focus_left);
    frame_clear(&f);
    line_browser_frame(&b, &m, &f);
    CHECK(has_text(&f, "此线路暂无区间"));
    line_browser_key(&b, &m, LINE_BACK, f.height);
    CHECK(!b.active && !b.paths.items);
    b.active = 1;
    size_t count = m.lines.rows.size;
    m.lines.rows.size = 0;
    frame_clear(&f);
    line_browser_frame(&b, &m, &f);
    CHECK(has_text(&f, "图集暂无线路"));
    m.lines.rows.size = count;
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

static void test_layout(MapDb *db, const char *name, int width, int export) {
    const Line *line = line_find_by_name(&db->metro.lines, name);
    CHECK(line != NULL);
    if (!line)
        return;
    MapFrame f = {0};
    CHECK(!frame_resize(&f, width, 200));
    LineBrowser b = {.active = 1, .selected = (int)(line - db->metro.lines.rows.items)};
    line_browser_frame(&b, &db->metro, &f);
    int left = width - (width >= 80 ? 24 : 16) - 1;
    int minx = width, maxx = 0, miny = f.height, maxy = 0, topx = -1, bottomx = -1;
    for (int y = 3; y < f.height - 3; y++) {
        for (int x = 0; x < left; x++) {
            if (!is_marker(f.cells[y * width + x].glyph))
                continue;
            if (x < minx) minx = x;
            if (x > maxx) maxx = x;
            if (y < miny) { miny = y; topx = x; }
            if (y > maxy) { maxy = y; bottomx = x; }
        }
    }
    if (!strcmp(name, "11号线")) {
        CHECK(topx > minx && topx < maxx);
        CHECK(bottomx > minx && bottomx < maxx);
        CHECK(maxy - miny > 3);
        int top_count = 0, bottom_count = 0, left_count = 0, right_count = 0;
        for (int y = miny; y <= maxy; y++) {
            for (int x = minx; x <= maxx; x++) {
                if (!is_marker(f.cells[y * width + x].glyph))
                    continue;
                CHECK(y == miny || y == maxy || x == minx || x == maxx);
                top_count += y == miny;
                bottom_count += y == maxy;
                left_count += x == minx;
                right_count += x == maxx;
            }
        }
        CHECK(left_count && right_count && top_count && bottom_count);
        CHECK(f.cells[miny * width + minx].glyph == 0x250c);
        CHECK(f.cells[miny * width + maxx].glyph == 0x2510);
        CHECK(f.cells[maxy * width + minx].glyph == 0x2514);
        CHECK(f.cells[maxy * width + maxx].glyph == 0x2518);
        if (width >= 140) {
            CHECK(top_count >= 3 && bottom_count >= 3);
            CHECK(b.row_count <= 50);
        }
    } else if (!strcmp(name, "3号线")) {
        if (width >= 140) CHECK(maxx > minx + 20);
        else CHECK(has_text(&f, "支线接续"));
    } else if (!strcmp(name, "12号线")) {
        CHECK(has_text(&f, "不连通"));
        if (width >= 140) CHECK(maxx > minx + 20);
        else CHECK(maxx == minx);
    }
    if (export) {
        int height = b.row_count + 6;
        if (height > 200) height = 200;
        if (height < 30) height = 30;
        CHECK(!frame_resize(&f, width, height));
        frame_clear(&f);
        line_browser_frame(&b, &db->metro, &f);
        char path[80];
        snprintf(path, sizeof(path), "line-layout-%d-%d.frame", line->id, width);
        dump_frame(&f, path);
    }
    line_browser_close(&b);
    frame_dispose(&f);
}

int main(int argc, char **argv) {
    if (argc < 2)
        return 2;
    test_synthetic();
    MapDb *db = calloc(1, sizeof(*db));
    CHECK(db && !map_db_open(db, argv[1]));
    if (!db || !db->db)
        return 1;
    TuiState s;
    CHECK(!tui_init(&s, db, 140, 40));
    s.from = station_find_by_name(&db->metro.stations, "体育西路")->id;
    s.to = station_find_by_name(&db->metro.stations, "广州南站")->id;
    tui_plan(&s, 140, 40);
    CHECK(s.ready);
    Viewport view = s.view;
    int *route = s.route.stations.items;
    s.scroll = 2;
    s.browser.active = 1;
    MapFrame f = {0};
    CHECK(!frame_resize(&f, 140, 40));
    for (size_t i = 0; i < db->metro.lines.rows.size; i++) {
        tui_frame(&s, &f);
        CHECK(s.browser.selected == (int)i);
        CHECK(s.browser.line_id == db->metro.lines.rows.items[i].id);
        CHECK(s.browser.row_count > 0 && !s.browser.focus_left);
        check_paths(&db->metro, s.browser.line_id, &s.browser.paths);
        check_coverage(&db->metro, s.browser.line_id, &s.browser.paths, 115);
        check_coverage(&db->metro, s.browser.line_id, &s.browser.paths, 33);
        line_browser_key(&s.browser, &db->metro, LINE_DOWN, f.height);
    }
    line_browser_key(&s.browser, &db->metro, LINE_TAB, f.height);
    int selected = s.browser.selected;
    for (int i = 0; i < 100; i++)
        line_browser_key(&s.browser, &db->metro, LINE_PAGE_DOWN, f.height);
    CHECK(s.browser.selected == selected);
    CHECK(s.browser.scroll == s.browser.row_count - (f.height - 6));
    CHECK(!frame_resize(&f, 50, 16));
    tui_frame(&s, &f);
    CHECK(!frame_resize(&f, 140, 200));
    tui_frame(&s, &f);
    CHECK(s.browser.scroll == 0);
    line_browser_key(&s.browser, &db->metro, LINE_BACK, f.height);
    CHECK(!s.browser.active && !s.browser.paths.items);
    CHECK(s.ready && s.route.stations.items == route && s.scroll == 2);
    CHECK(s.view.x == view.x && s.view.y == view.y && s.view.scale == view.scale);
    frame_dispose(&f);
    tui_dispose(&s);
    const char *layouts[] = {"3号线", "11号线", "12号线"};
    for (size_t i = 0; i < sizeof(layouts) / sizeof(layouts[0]); i++) {
        test_layout(db, layouts[i], 140, argc == 3);
        test_layout(db, layouts[i], 50, argc == 3);
    }
    map_db_close(db);
    free(db);
    printf("%d line browser checks failed\n", failures);
    return failures ? 1 : 0;
}
