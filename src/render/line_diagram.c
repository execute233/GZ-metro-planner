#include "line_diagram.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct { int begin, end, used; } Run;
typedef struct {
    MapFrame *frame;
    const Metro *metro;
    const ArrayList_Int *paths;
    int line_id, scroll, width, count;
    Run *runs;
    unsigned char *strokes;
    int parent[MAP_LIMIT], drawn[MAP_LIMIT];
} Diagram;

static int root(Diagram *d, int id) {
    while (d->parent[id] != id) {
        d->parent[id] = d->parent[d->parent[id]];
        id = d->parent[id];
    }
    return id;
}

static void glyph(Diagram *d, int x, int y, uint32_t cp, int color) {
    y += 3 - d->scroll;
    if (x >= 1 && x < d->width - 1 && y >= 3 && y < d->frame->height - 3)
        frame_glyph(d->frame, x, y, cp, color, 0);
}

/* Also used with paint=0 to measure wrapped labels before placing tracks. */
static int text(Diagram *d, int x, int y, int width, const char *s, int color, int paint) {
    int column = 0, rows = 1;
    uint32_t cp;
    while (utf8_next(&s, &cp)) {
        int w = unicode_width(cp);
        if (column + w > width) {
            rows++;
            column = 0;
        }
        if (paint)
            glyph(d, x + column, y + rows - 1, cp, color);
        column += w;
    }
    return rows;
}

static int label_text(Diagram *d, int x, int y, int width, const char *value, int color,
                      int paint, int right) {
    int cells = 0;
    uint32_t cp;
    const char *p = value;
    while (utf8_next(&p, &cp))
        cells += unicode_width(cp);
    int offset = right && cells < width ? width - cells : 0;
    return text(d, x + offset, y, width - offset, value, color, paint);
}

static int label(Diagram *d, int id, int x, int y, int width, int paint, int right) {
    const Station *s = station_find_by_id(&d->metro->stations, id);
    int rows = label_text(d, x, y, width, s->name, 4 + d->line_id, paint, right);
    for (size_t i = 0; i < d->metro->lines.rows.size; i++) {
        const Line *line = &d->metro->lines.rows.items[i];
        if (line->id == d->line_id)
            continue;
        for (size_t j = 0; j < d->metro->edges.rows.size; j++) {
            const Edge *e = &d->metro->edges.rows.items[j];
            if (e->line_id == line->id && (e->from_station_id == id || e->to_station_id == id)) {
                char value[LINE_NAME_MAX + 16];
                snprintf(value, sizeof(value), "换乘 %s", line->name);
                rows += label_text(d, x, y + rows, width, value, 4 + line->id, paint, right);
                break;
            }
        }
    }
    return rows;
}

static void stroke(Diagram *d, int x, int y, unsigned direction) {
    static const uint32_t shapes[16] = {
        0, 0x2502, 0x2500, 0x2514, 0x2502, 0x2502, 0x250c, 0x251c,
        0x2500, 0x2518, 0x2500, 0x2534, 0x2510, 0x2524, 0x252c, 0x253c
    };
    int screen_y = y + 3 - d->scroll;
    if (x < 1 || x >= d->width - 1 || screen_y < 3 || screen_y >= d->frame->height - 3)
        return;
    unsigned char *mask = &d->strokes[screen_y * d->frame->width + x];
    *mask |= direction;
    glyph(d, x, y, shapes[*mask], 4 + d->line_id);
}

static void track(Diagram *d, int x0, int y0, int x1, int y1) {
    int dx = abs(x1 - x0), dy = -abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, error = dx + dy;
    while (x0 != x1 || y0 != y1) {
        int twice = 2 * error;
        if (twice >= dy) {
            stroke(d, x0, y0, sx > 0 ? 2 : 8);
            error += dy; x0 += sx;
            stroke(d, x0, y0, sx > 0 ? 8 : 2);
        }
        if (twice <= dx) {
            stroke(d, x0, y0, sy > 0 ? 4 : 1);
            error += dx; y0 += sy;
            stroke(d, x0, y0, sy > 0 ? 1 : 4);
        }
    }
}

static void marker(Diagram *d, int id, int x, int y) {
    int transfer = 0;
    for (size_t i = 0; i < d->metro->edges.rows.size; i++) {
        const Edge *e = &d->metro->edges.rows.items[i];
        if (e->line_id != d->line_id && (e->from_station_id == id || e->to_station_id == id))
            transfer = 1;
    }
    glyph(d, x, y, transfer ? 0x25c6 : 0x25cf, 4 + d->line_id);
    d->drawn[id] = 1;
}

static int endpoint(Diagram *d, Run *r, int end) {
    return d->paths->items[end ? r->end : r->begin];
}

/* Render each run once. Shared junctions get one marker; narrow branches have
 * explicit continuation labels instead of a misleading vertical connection. */
static int branch(Diagram *d, int index, int start, int x, int y, int width, int skip_start) {
    Run *r = &d->runs[index];
    r->used = 1;
    int step = endpoint(d, r, 0) == start ? 1 : -1;
    int i = step > 0 ? r->begin : r->end;
    int end = step > 0 ? r->end : r->begin;
    if (skip_start)
        i += step;
    int last_y = y, last_height = 1, last_id = start;
    for (;;) {
        int id = d->paths->items[i];
        if (d->drawn[id]) {
            const Station *station = station_find_by_id(&d->metro->stations, id);
            char value[STATION_NAME_MAX + 24];
            snprintf(value, sizeof(value), "接回：%s", station->name);
            return y + text(d, x, y, width, value, 0, 1) + 2;
        }
        int height = label(d, id, x + 3, y, width - 3, 1, 0);
        marker(d, id, x, y);
        last_y = y;
        last_height = height;
        last_id = id;
        y += height + 2;
        if (i == end)
            break;
        track(d, x, last_y + 1, x, y - 1);
        i += step;
    }
    int children = 0;
    for (int j = 0; j < d->count; j++)
        if (!d->runs[j].used && (endpoint(d, &d->runs[j], 0) == last_id ||
                                 endpoint(d, &d->runs[j], 1) == last_id))
            children++;
    int bottom = y, column = 0;
    int parallel = children > 1 && width / children >= 24;
    if (parallel) {
        int split_y = last_y + last_height;
        track(d, x, last_y + 1, x, split_y);
        track(d, x, split_y, x + (children - 1) * (width / children), split_y);
    }
    for (int j = 0; j < d->count; j++) {
        Run *next = &d->runs[j];
        if (next->used || (endpoint(d, next, 0) != last_id && endpoint(d, next, 1) != last_id))
            continue;
        int child_x = parallel ? x + column * (width / children) : x;
        int child_y = parallel || children == 1 ? y : bottom;
        int child_width = parallel ? width / children - 2 : width;
        if (parallel || children == 1) {
            track(d, child_x, children == 1 ? last_y + 1 : last_y + last_height,
                  child_x, child_y);
        } else {
            const Station *station = station_find_by_id(&d->metro->stations, last_id);
            char value[STATION_NAME_MAX + 24];
            snprintf(value, sizeof(value), "支线接续：%s", station->name);
            child_y += text(d, child_x, child_y, child_width, value, 0, 1) + 1;
        }
        int child_bottom = branch(d, j, last_id, child_x, child_y, child_width, 1);
        if (child_bottom > bottom)
            bottom = child_bottom;
        column++;
    }
    return bottom;
}

typedef struct { int x, y, label_x, label_y, width, height; } RingStation;

/* A schematic rectangular loop: stations occupy all four sides in route order.
 * Horizontal slots share a baseline; vertical pairs reserve their actual label
 * heights, so one long transfer label cannot stretch every station interval. */
static int ring(Diagram *d, Run *run, int x, int y, int width) {
    int n = run->end - run->begin;
    RingStation *points = calloc((size_t)n, sizeof(*points));
    if (!points)
        return -1;
    int across = (width - 4) / 14;
    if (across > n / 4) across = n / 4;
    if (across < 1) across = 1;
    int right_count = (n - 2 * across + 1) / 2;
    int left_count = n - 2 * across - right_count;
    int left_x = x + 1, right_x = x + width - 2;
    int slot = (width - 4) / across;
    int side_width = (width - 8) / 2;
    int top_height = 1;
    for (int i = 0; i < n; i++) {
        int horizontal = i < across || (i >= across + right_count && i < 2 * across + right_count);
        points[i].width = horizontal ? slot - 1 : side_width;
        points[i].height = label(d, d->paths->items[run->begin + i], 0, 0, points[i].width, 0, 0);
        if (i < across && points[i].height > top_height)
            top_height = points[i].height;
    }
    int top = y + top_height + 1;
    for (int i = 0; i < across; i++) {
        RingStation *p = &points[i];
        p->label_x = x + 2 + i * slot;
        p->x = p->label_x + (slot - 1) / 2;
        p->y = top;
        p->label_y = top - p->height - 1;
    }
    int row = top + 2;
    for (int i = 0; i < right_count; i++) {
        RingStation *r = &points[across + i];
        r->x = right_x;
        r->y = r->label_y = row;
        r->label_x = right_x - side_width - 2;
        int height = r->height;
        if (i < left_count) {
            RingStation *l = &points[n - 1 - i];
            l->x = left_x;
            l->y = l->label_y = row;
            l->label_x = left_x + 3;
            if (l->height > height) height = l->height;
        }
        row += height + 1;
    }
    int bottom = row + 1, end = bottom + 2;
    for (int j = 0; j < across; j++) {
        RingStation *p = &points[across + right_count + j];
        p->label_x = x + 2 + (across - 1 - j) * slot;
        p->x = p->label_x + (slot - 1) / 2;
        p->y = bottom;
        p->label_y = bottom + 2;
        if (p->label_y + p->height > end) end = p->label_y + p->height;
    }
    track(d, left_x, top, right_x, top);
    track(d, right_x, top, right_x, bottom);
    track(d, right_x, bottom, left_x, bottom);
    track(d, left_x, bottom, left_x, top);
    for (int i = 0; i < n; i++) {
        int id = d->paths->items[run->begin + i];
        marker(d, id, points[i].x, points[i].y);
        label(d, id, points[i].label_x, points[i].label_y, points[i].width, 1,
              i >= across && i < across + right_count);
    }
    run->used = 1;
    free(points);
    return end + 1;
}

int line_diagram_render(MapFrame *frame, const Metro *metro, int line_id,
                        const ArrayList_Int *paths, int width, int scroll) {
    if (!paths->size)
        return 0;
    Diagram *d = calloc(1, sizeof(*d));
    if (!d)
        return -1;
    *d = (Diagram){.frame = frame, .metro = metro, .paths = paths,
                   .line_id = line_id, .scroll = scroll, .width = width};
    d->runs = calloc(paths->size, sizeof(*d->runs));
    d->strokes = calloc((size_t)frame->width * frame->height, 1);
    if (!d->runs || !d->strokes) { free(d->runs); free(d->strokes); free(d); return -1; }
    for (int i = 0; i < MAP_LIMIT; i++)
        d->parent[i] = i;
    for (size_t i = 0; i < paths->size;) {
        int begin = (int)i;
        while (i + 1 < paths->size && paths->items[i + 1]) {
            int a = root(d, paths->items[i]), b = root(d, paths->items[i + 1]);
            d->parent[b] = a;
            i++;
        }
        d->runs[d->count++] = (Run){begin, (int)i, 0};
        i += 2;
    }
    int components = 0;
    for (int i = 0; i < d->count; i++) {
        int component = root(d, endpoint(d, &d->runs[i], 0)), seen = 0;
        for (int j = 0; j < i; j++)
            if (root(d, endpoint(d, &d->runs[j], 0)) == component)
                seen = 1;
        if (!seen)
            components++;
    }
    int columns = components > 1 && (width - 4) / components >= 34 ? components : 1;
    int block_width = (width - 4) / columns, bottom = 0, column = 0;
    for (int i = 0; i < d->count; i++) {
        if (d->runs[i].used)
            continue;
        Run *r = &d->runs[i];
        int x = 2 + column * block_width, y = columns > 1 ? 0 : bottom;
        int component = root(d, endpoint(d, r, 0));
        int start = endpoint(d, r, 0);
        /* Prefer a leaf as the trunk's beginning; do not duplicate a junction. */
        for (int j = i; j < d->count; j++) {
            if (root(d, endpoint(d, &d->runs[j], 0)) != component)
                continue;
            for (int side = 0; side < 2; side++) {
                int id = endpoint(d, &d->runs[j], side), degree = 0;
                for (int k = 0; k < d->count; k++)
                    degree += (endpoint(d, &d->runs[k], 0) == id) + (endpoint(d, &d->runs[k], 1) == id);
                if (degree == 1) { r = &d->runs[j]; start = id; goto found; }
            }
        }
found:
        if (components > 1) {
            char title[2 * STATION_NAME_MAX + 32];
            snprintf(title, sizeof(title), "%s — %s（独立区段）",
                     station_find_by_id(&metro->stations, endpoint(d, r, 0))->name,
                     station_find_by_id(&metro->stations, endpoint(d, r, 1))->name);
            y += text(d, x, y, block_width - 2, title, 0, 1);
            y += text(d, x, y, block_width - 2, "与其他区段不连通", 0, 1) + 1;
        }
        int next = endpoint(d, r, 0) == endpoint(d, r, 1)
                       ? ring(d, r, x, y, block_width - 2)
                       : branch(d, (int)(r - d->runs), start, x, y, block_width - 2, 0);
        if (next < 0) { bottom = -1; break; }
        if (next > bottom)
            bottom = next + 2;
        if (columns > 1)
            column++;
    }
    free(d->runs);
    free(d->strokes);
    free(d);
    return bottom;
}
