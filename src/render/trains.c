#include "trains.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static double distance(double x, double y, double a, double b) {
    return hypot(x - a, y - b);
}
static void track_dispose(TrainTrack *track) {
    free(track->steps);
    memset(track, 0, sizeof(*track));
}
void trains_dispose(Trains *t) {
    for (size_t i = 0; i < t->count; i++) {
        free(t->geometry[i].segments);
        free(t->geometry[i].ends);
    }
    for (size_t i = 0; i < t->line_count; i++)
        track_dispose(&t->lines[i]);
    free(t->lines);
    free(t->geometry);
    track_dispose(&t->journey);
    memset(t, 0, sizeof(*t));
}
static int append(Trains *t, TrainTrack *track, int geometry, int reverse) {
    TrainStep *steps = realloc(track->steps, (track->count + 1) * sizeof(*steps));
    if (!steps)
        return -1;
    track->steps = steps;
    /* Half a wall-clock second hides discontinuous transitions between branches. */
    track->duration += geometry < 0 ? .5 * TRAIN_SPEED_MULTIPLIER
                                   : t->geometry[geometry].edge.cost_time_second;
    track->steps[track->count++] = (TrainStep){geometry, reverse, track->duration};
    return 0;
}
static int degree(const Trains *t, int line, int station) {
    int n = 0;
    for (size_t i = 0; i < t->count; i++) {
        Edge e = t->geometry[i].edge;
        if (e.line_id == line && (e.from_station_id == station || e.to_station_id == station))
            n++;
    }
    return n;
}
/* Partition at endpoints/junctions; each edge belongs to one continuous trail.
 * Open trails return along themselves, rings continue forward, disconnected
 * trails switch only while invisible. One track still means one train per line. */
static int network_track(Trains *t, TrainTrack *track, int line) {
    unsigned char *used = calloc(t->count, 1);
    if (!used)
        return -1;
    int result = -1;
    for (;;) {
        int first = -1, start = 0;
        for (size_t i = 0; i < t->count; i++) {
            Edge e = t->geometry[i].edge;
            if (used[i] || e.line_id != line)
                continue;
            if (first < 0) {
                first = (int)i;
                start = e.from_station_id;
            }
            if (degree(t, line, e.from_station_id) != 2) {
                first = (int)i;
                start = e.from_station_id;
                break;
            }
            if (degree(t, line, e.to_station_id) != 2) {
                first = (int)i;
                start = e.to_station_id;
                break;
            }
        }
        if (first < 0)
            break;
        size_t begin = track->count;
        int station = start, next = first;
        while (next >= 0) {
            Edge e = t->geometry[next].edge;
            int reverse = e.to_station_id == station;
            if (append(t, track, next, reverse))
                goto done;
            used[next] = 1;
            station = reverse ? e.from_station_id : e.to_station_id;
            if (station == start || degree(t, line, station) != 2)
                break;
            next = -1;
            for (size_t i = 0; i < t->count; i++) {
                e = t->geometry[i].edge;
                if (!used[i] && e.line_id == line &&
                    (e.from_station_id == station || e.to_station_id == station)) {
                    next = (int)i;
                    break;
                }
            }
        }
        if (station != start) {
            size_t end = track->count;
            for (size_t i = end; i > begin; i--) {
                TrainStep step = track->steps[i - 1];
                if (append(t, track, step.geometry, !step.reverse))
                    goto done;
            }
        }
        if (append(t, track, -1, 0))
            goto done;
    }
    /* A single continuous path needs no invisible transition when looping. */
    size_t gaps = 0;
    for (size_t i = 0; i < track->count; i++)
        gaps += track->steps[i].geometry < 0;
    if (gaps == 1 && track->count > 1) {
        track->count--;
        track->duration = track->steps[track->count - 1].end;
    }
    result = 0;
done:
    free(used);
    return result;
}

static int load_geometry(TrainGeometry *g, const MapSegments *tile, const MapDb *map) {
    for (size_t i = 0; i < tile->count; i++) {
        MapSegment s = tile->items[i];
        if (s.edge_id == g->edge.id && distance(s.x0, s.y0, s.x1, s.y1) > 0)
            g->count++;
    }
    if (!g->count)
        return -1;
    g->segments = malloc(g->count * sizeof(*g->segments));
    g->ends = malloc(g->count * sizeof(*g->ends));
    if (!g->segments || !g->ends)
        return -1;
    size_t n = 0;
    for (size_t i = 0; i < tile->count; i++) {
        MapSegment s = tile->items[i];
        if (s.edge_id == g->edge.id && distance(s.x0, s.y0, s.x1, s.y1) > 0)
            g->segments[n++] = s;
    }
    MapStation station = map->stations[g->edge.from_station_id];
    double x = station.x, y = station.y;
    for (size_t i = 0; i < g->count; i++) {
        size_t best = i;
        int reverse = 0;
        double nearest = HUGE_VAL;
        for (size_t j = i; j < g->count; j++) {
            MapSegment s = g->segments[j];
            double a = distance(x, y, s.x0, s.y0), b = distance(x, y, s.x1, s.y1);
            if (fmin(a, b) < nearest) {
                nearest = fmin(a, b);
                best = j;
                reverse = b < a;
            }
        }
        /* z=0 quantization is one schematic pixel. Never bridge absent geometry. */
        if (nearest > 2)
            return -1;
        MapSegment s = g->segments[best];
        g->segments[best] = g->segments[i];
        if (reverse)
            s = (MapSegment){s.x1, s.y1, s.x0, s.y0, s.edge_id};
        g->segments[i] = s;
        g->length += distance(s.x0, s.y0, s.x1, s.y1);
        g->ends[i] = g->length;
        x = s.x1;
        y = s.y1;
    }
    station = map->stations[g->edge.to_station_id];
    return distance(x, y, station.x, station.y) <= 2 ? 0 : -1;
}
int trains_init(Trains *t, MapDb *map) {
    memset(t, 0, sizeof(*t));
    const MapSegments *tile;
    if (map_db_tile(map, 0, 0, 0, &tile))
        return -1;
    t->count = map->metro.edges.rows.size;
    t->line_count = map->metro.lines.rows.size;
    t->geometry = calloc(t->count ? t->count : 1, sizeof(*t->geometry));
    t->lines = calloc(t->line_count ? t->line_count : 1, sizeof(*t->lines));
    if (!t->geometry || !t->lines) {
        free(t->geometry);
        free(t->lines);
        memset(t, 0, sizeof(*t));
        return -1;
    }
    for (size_t i = 0; i < t->count; i++) {
        t->geometry[i].edge = map->metro.edges.rows.items[i];
        if (load_geometry(&t->geometry[i], tile, map))
            goto bad;
    }
    for (size_t i = 0; i < t->line_count; i++)
        if (network_track(t, &t->lines[i], map->metro.lines.rows.items[i].id))
            goto bad;
    return 0;
bad:
    trains_dispose(t);
    return -1;
}
int trains_plan(Trains *t, const Route *route) {
    track_dispose(&t->journey);
    t->journey_time = 0;
    if (!route)
        return 0;
    if (route->stations.size != route->edges_ids.size + 1)
        return -1;
    for (size_t i = 0; i < route->edges_ids.size; i++) {
        size_t j = 0;
        while (j < t->count && t->geometry[j].edge.id != route->edges_ids.items[i])
            j++;
        if (j == t->count)
            goto bad;
        Edge e = t->geometry[j].edge;
        int a = route->stations.items[i], b = route->stations.items[i + 1];
        if (!((e.from_station_id == a && e.to_station_id == b) ||
              (e.to_station_id == a && e.from_station_id == b)))
            goto bad;
        if (append(t, &t->journey, (int)j, e.to_station_id == a))
            goto bad;
    }
    return 0;
bad:
    track_dispose(&t->journey);
    return -1;
}
void trains_advance(Trains *t, double elapsed, int maintenance) {
    if (t->paused || maintenance || !isfinite(elapsed) || elapsed <= 0)
        return;
    t->network_time += elapsed * TRAIN_SPEED_MULTIPLIER;
    t->journey_time += elapsed * TRAIN_SPEED_MULTIPLIER;
}
int trains_position(const Trains *t, const TrainTrack *track, double time, TrainPosition *out) {
    if (!track->count || track->duration <= 0 || !isfinite(time) || time < 0)
        return 0;
    time = fmod(time, track->duration);
    size_t i = 0;
    while (i + 1 < track->count && time >= track->steps[i].end)
        i++;
    TrainStep step = track->steps[i];
    if (step.geometry < 0)
        return 0;
    const TrainGeometry *g = &t->geometry[step.geometry];
    double start = i ? track->steps[i - 1].end : 0;
    double fraction = (time - start) / (step.end - start);
    double length = (step.reverse ? 1 - fraction : fraction) * g->length;
    size_t lo = 0, hi = g->count - 1;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (g->ends[mid] < length) lo = mid + 1;
        else hi = mid;
    }
    MapSegment s = g->segments[lo];
    double before = lo ? g->ends[lo - 1] : 0;
    fraction = (length - before) / (g->ends[lo] - before);
    /* Average over nearby geometry so one-pixel MVT steps do not flicker
     * between horizontal and vertical headings on a diagonal curve. */
    size_t left = lo, right = lo;
    while (left > 0 && g->ends[left - 1] > length - 6)
        left--;
    while (right + 1 < g->count && g->ends[right] < length + 6)
        right++;
    double dx = g->segments[right].x1 - g->segments[left].x0;
    double dy = g->segments[right].y1 - g->segments[left].y0;
    *out = (TrainPosition){s.x0 + (s.x1 - s.x0) * fraction,
                          s.y0 + (s.y1 - s.y0) * fraction,
                          dx * (step.reverse ? -1 : 1),
                          dy * (step.reverse ? -1 : 1),
                          g->edge.line_id, g->edge.id};
    return 1;
}
static int free_cell(const MapFrame *f, int x, int y, int cols, int rows) {
    if (x < 0 || y < 0 || x >= cols || y >= rows)
        return 0;
    MapCell c = f->cells[y * f->width + x];
    return !c.continuation && (!c.glyph || (c.glyph >= 0x2800 && c.glyph <= 0x28ff));
}
static void draw_train(MapFrame *f, Viewport v, TrainPosition p, int cols, int rows) {
    int x = (int)floor(((p.x - v.x) * v.scale + cols) / 2);
    int y = (int)floor(((p.y - v.y) * v.scale + rows * 2) / 4);
    if (!free_cell(f, x, y, cols, rows))
        return;
    int dx = 0, dy = 0;
    if (fabs(p.dx) >= fabs(p.dy)) dx = p.dx >= 0 ? 1 : -1;
    else dy = p.dy >= 0 ? 1 : -1;
    int large = v.scale >= TRAIN_CARRIAGE_SCALE;
    for (int i = 1; i <= 2 && large; i++)
        large = free_cell(f, x - i * dx, y - i * dy, cols, rows);
    uint32_t arrow = dx > 0 ? '>' : dx < 0 ? '<' : dy > 0 ? 'v' : '^';
    if (large) {
        arrow = dx > 0 ? 0x25b6 : dx < 0 ? 0x25c0 : dy > 0 ? 0x25bc : 0x25b2;
        for (int i = 1; i <= 2; i++)
            frame_glyph(f, x - i * dx, y - i * dy, 0x25b0, 4 + p.line_id, 0);
    }
    frame_glyph(f, x, y, arrow, 4 + p.line_id, 0);
}
void trains_render(const Trains *t, int journey, MapFrame *f, Viewport v, int cols, int rows) {
    if (cols < 1 || rows < 1 || cols > f->width || rows > f->height ||
        !isfinite(v.scale) || v.scale <= 0)
        return;
    size_t count = journey ? 1 : t->line_count;
    for (size_t i = 0; i < count; i++) {
        const TrainTrack *track = journey ? &t->journey : &t->lines[i];
        /* Stable stagger keeps the overview from starting every train at a terminus. */
        double time = journey ? t->journey_time : t->network_time + track->duration * fmod((i + 1) * .61803398875, 1);
        TrainPosition p;
        if (trains_position(t, track, time, &p))
            draw_train(f, v, p, cols, rows);
    }
}
