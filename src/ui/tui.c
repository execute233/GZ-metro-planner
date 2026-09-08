#include "tui.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int map_width(int width) {
    return width >= 90 ? width - 37 : width;
}
static const char *name(TuiState *s, int id) {
    Station *st = station_find_by_id(&s->map->metro.stations, id);
    return st ? st->name : "未选择";
}
int tui_init(TuiState *s, MapDb *m, int w, int h) {
    memset(s, 0, sizeof(*s));
    s->map = m;
    route_init(&s->route);
    if (graph_build(&s->graph, &m->metro))
        return -1;
    viewport_fit(&s->view, m, NULL, map_width(w), h - 3);
    snprintf(s->status, sizeof(s->status), "每区间暂定 1000m / 60s；数据待人工校对");
    tui_search(s);
    return 0;
}
void tui_dispose(TuiState *s) {
    route_dispose(&s->route);
    graph_dispose(&s->graph);
}
void tui_search(TuiState *s) {
    char query[128];
    size_t n = strlen(s->query);
    for (size_t i = 0; i <= n; i++)
        query[i] = (char)tolower((unsigned char)s->query[i]);
    s->match_count = 0;
    for (size_t i = 0; i < s->map->metro.stations.rows.size; i++) {
        Station *st = &s->map->metro.stations.rows.items[i];
        if (!*query || strstr(st->name, query) || strstr(st->pinyin_name, query) ||
            strstr(s->map->stations[st->id].initials, query))
            s->matches[s->match_count++] = st->id;
    }
    if (s->candidate >= s->match_count)
        s->candidate = s->match_count ? s->match_count - 1 : 0;
}
void tui_plan(TuiState *s, int w, int h) {
    s->ready = 0;
    s->scroll = 0;
    route_dispose(&s->route);
    route_init(&s->route);
    if (!s->from || !s->to)
        return;
    if (router_find_route(&s->graph, &s->map->metro, s->from, s->to, (RouteMetric)s->metric,
                          &s->route)) {
        snprintf(s->status, sizeof(s->status), "当前图集内两站不可达");
        return;
    }
    s->ready = 1;
    viewport_fit(&s->view, s->map, &s->route, map_width(w), h - 3);
    snprintf(s->status, sizeof(s->status), "%d 站（含起终点） / %.1f km / %d 分钟；暂定权重",
             s->route.total_stations, s->route.total_meters / 1000., s->route.total_seconds / 60);
}
static void sidebar(TuiState *s, MapFrame *f, int x, int width, int height) {
    char text[256];
    int inside = width - 3;
    frame_text(f, x + 2, 1, inside, "广州地铁 · 路线规划", 0, 0);
    snprintf(text, sizeof(text), "%s起点：%s", s->focus == 1 ? "> " : "", name(s, s->from));
    frame_text(f, x + 2, 3, inside, text, s->focus == 1 ? 1 : 0, 0);
    snprintf(text, sizeof(text), "%s终点：%s", s->focus == 2 ? "> " : "", name(s, s->to));
    frame_text(f, x + 2, 5, inside, text, s->focus == 2 ? 1 : 0, 0);
    const char *metrics[] = {"最少站点", "最短距离", "最少时间"};
    snprintf(text, sizeof(text), "%s目标：%s", s->focus == 3 ? "> " : "", metrics[s->metric]);
    frame_text(f, x + 2, 7, inside, text, s->focus == 3 ? 1 : 0, 0);
    int row = 10;
    if (s->focus == 1 || s->focus == 2) {
        snprintf(text, sizeof(text), "搜索：%s_", s->query);
        frame_text(f, x + 2, 9, inside, text, 1, 0);
        int visible = height - 13;
        if (visible > 9)
            visible = 9;
        if (visible < 1)
            visible = 1;
        int start = s->candidate >= visible ? s->candidate - visible + 1 : 0;
        for (int i = start; i < s->match_count && i < start + visible; i++) {
            int id = s->matches[i];
            snprintf(text, sizeof(text), "%s %s", i == s->candidate ? ">" : " ", name(s, id));
            frame_text(f, x + 2, row++, inside, text, i == s->candidate ? 1 : 0, 0);
        }
        if (!s->match_count)
            frame_text(f, x + 2, row++, inside, "无匹配站点", 0, 0);
        if (s->match_count) {
            int id = s->matches[s->candidate];
            char lines[256] = "线路：";
            for (size_t k = 0; k < s->map->metro.lines.rows.size; k++) {
                Line *ln = &s->map->metro.lines.rows.items[k];
                int member = 0;
                for (size_t j = 0; j < s->map->metro.edges.rows.size; j++) {
                    Edge *e = &s->map->metro.edges.rows.items[j];
                    if (e->line_id == ln->id &&
                        (e->from_station_id == id || e->to_station_id == id))
                        member = 1;
                }
                if (member && strlen(lines) + strlen(ln->name) + 2 < sizeof(lines)) {
                    strcat(lines, ln->name);
                    strcat(lines, " ");
                }
            }
            frame_text(f, x + 2, row++, inside, lines, 0, 1);
        }
        frame_text(f, x + 2, row++, inside, "↑↓ 选择  Enter 确认  Esc 返回", 0, 1);
    }
    if (s->ready) {
        row++;
        snprintf(text, sizeof(text), "%d 站 · %.1fkm · %dmin", s->route.total_stations,
                 s->route.total_meters / 1000., s->route.total_seconds / 60);
        frame_text(f, x + 2, row++, inside, text, 0, 0);
        snprintf(text, sizeof(text), "换乘 %zu 次 · 里程/时间暂定", s->route.transfers.size);
        frame_text(f, x + 2, row++, inside, text, 0, 1);
        for (size_t i = (size_t)s->scroll; i < s->route.stations.size && row < height - 1; i++) {
            int color = 0;
            const char *line_name = "起点";
            if (i) {
                Edge *e = edge_find_by_id(&s->map->metro.edges, s->route.edges_ids.items[i - 1]);
                Line *ln = line_find_by_id(&s->map->metro.lines, e->line_id);
                line_name = ln ? ln->name : "?";
                color = 4 + e->line_id;
            }
            snprintf(text, sizeof(text), "%s · %s", name(s, s->route.stations.items[i]), line_name);
            frame_text(f, x + 2, row++, inside, text, color, 0);
        }
    } else if (s->focus == 0) {
        frame_text(f, x + 2, 11, inside, "Tab 或 / 开始搜索", 0, 0);
        frame_text(f, x + 2, 13, inside, "确认起终点后自动规划", 0, 1);
    }
}
void tui_frame(TuiState *s, MapFrame *f) {
    frame_clear(f);
    int w = f->width, h = f->height;
    if (w < 50 || h < 16) {
        frame_text(f, 0, 0, w, "窗口至少需要 50×16；Q 退出", 0, 0);
        return;
    }
    int mw = map_width(w), mh = h - 3;
    int compact = w < 90 && s->focus != 0;
    if (!compact) {
        int errors =
            map_render(f, s->map, s->view, s->ready ? &s->route : NULL, s->from, s->to, mw, mh);
        if (errors)
            snprintf(s->status, sizeof(s->status), "%d 个瓦片读取失败；路线计算仍可用", errors);
        frame_text(f, 1, 0, mw - 2, "广州地铁 · 全网示意图", 0, 0);
    }
    if (w >= 90) {
        for (int y = 0; y < mh; y++)
            frame_glyph(f, mw, y, 0x2502, 0, 0);
        sidebar(s, f, mw + 1, w - mw - 1, mh);
    } else if (compact)
        sidebar(s, f, 0, w, mh);
    for (int x = 0; x < w; x++)
        frame_glyph(f, x, h - 3, 0x2500, 0, 0);
    frame_text(f, 0, h - 2, w,
               s->focus == 0
                   ? "Tab 搜索  WASD/方向键 平移  +/- 缩放  R 全图  F 路线  X 交换  Q 退出"
                   : "Tab 焦点  ↑↓ 候选  Enter 确认  Esc 地图  PgUp/PgDn 行程",
               0, 0);
    frame_text(f, 0, h - 1, w, s->status, 0, 1);
}
static void json_string(FILE *f, uint32_t cp) {
    if (cp == '"' || cp == '\\')
        fprintf(f, "\\%c", (int)cp);
    else if (cp < 0x80)
        fputc((int)cp, f);
    else if (cp < 0x800) {
        fputc(0xc0 | (cp >> 6), f);
        fputc(0x80 | (cp & 63), f);
    } else if (cp < 0x10000) {
        fputc(0xe0 | (cp >> 12), f);
        fputc(0x80 | ((cp >> 6) & 63), f);
        fputc(0x80 | (cp & 63), f);
    } else {
        fputc(0xf0 | (cp >> 18), f);
        fputc(0x80 | ((cp >> 12) & 63), f);
        fputc(0x80 | ((cp >> 6) & 63), f);
        fputc(0x80 | (cp & 63), f);
    }
}
int tui_snapshot(const char *path, const char *output, int w, int h, const char *from,
                 const char *to) {
    MapDb *db = calloc(1, sizeof(*db));
    TuiState s;
    MapFrame f = {0};
    int result = -1;
    if (!db)
        return -1;
    if (map_db_open(db, path)) {
        fprintf(stderr, "%s\n", db->error);
        free(db);
        return -1;
    }
    if (tui_init(&s, db, w, h))
        goto cleanup;
    if (from && to) {
        snprintf(s.query, sizeof(s.query), "%s", from);
        tui_search(&s);
        if (s.match_count)
            s.from = s.matches[0];
        snprintf(s.query, sizeof(s.query), "%s", to);
        tui_search(&s);
        if (s.match_count)
            s.to = s.matches[0];
        s.query[0] = 0;
        tui_plan(&s, w, h);
    }
    if (frame_resize(&f, w, h) == 0) {
        tui_frame(&s, &f);
        FILE *file = fopen(output, "wb");
        if (file) {
            fprintf(file, "{\"width\":%d,\"height\":%d,\"colors\":[", w, h);
            for (int i = 0; i < 128; i++)
                fprintf(file, "%s%u", i ? "," : "", map_display_color(db->colors[i]));
            fprintf(file, "],\"cells\":[");
            for (int i = 0; i < w * h; i++) {
                MapCell c = f.cells[i];
                fprintf(file, "%s[\"", i ? "," : "");
                json_string(file, c.glyph ? c.glyph : ' ');
                fprintf(file, "\",%d,%d,%d]", c.color, c.dim, c.continuation);
            }
            fprintf(file, "]}\n");
            int failed = ferror(file);
            if (fclose(file))
                failed = 1;
            result = failed ? -1 : 0;
        }
    }
    frame_dispose(&f);
    tui_dispose(&s);
cleanup:
    map_db_close(db);
    free(db);
    return result;
}
