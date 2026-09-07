#include "map_renderer.h"
#include "render.h"
#include <stdlib.h>
#include <math.h>
#include <limits.h>

static bool contains(const ArrayList_Int *ids, int id) {
    if (ids) for (size_t i = 0; i < ids->size; i++) if (ids->items[i] == id) return true;
    return false;
}
void map_bounds(const MapDocument *map, const Route *route, MapPoint *min, MapPoint *max) {
    *min = (MapPoint){0, 0}; *max = (MapPoint){100, 100}; bool found = false;
    for (size_t i = 0; i < map->stations.size; i++) {
        const MapStation *s = &map->stations.items[i];
        if (route && !contains(&route->stations, s->station_id)) continue;
        if (!found) { *min = *max = s->point; found = true; }
        min->x = fmin(min->x, s->point.x); min->y = fmin(min->y, s->point.y);
        max->x = fmax(max->x, s->point.x); max->y = fmax(max->y, s->point.y);
    }
    for (size_t i = 0; i < map->vertices.size; i++) {
        const MapVertex *p = &map->vertices.items[i];
        if (route && !contains(&route->edges_ids, p->edge_id)) continue;
        min->x = fmin(min->x, p->point.x); min->y = fmin(min->y, p->point.y);
        max->x = fmax(max->x, p->point.x); max->y = fmax(max->y, p->point.y);
    }
}
int map_hit_test(const MapDocument *map, const Viewport *v, int col, int row) {
    double best = 10; int id = 0;
    for (size_t i = 0; i < map->stations.size; i++) {
        MapPoint p = viewport_project(v, map->stations.items[i].point);
        double dx = p.x/2-col, dy = p.y/4-row;
        double d = dx*dx+dy*dy*4;
        if (d < best) { best = d; id = map->stations.items[i].station_id; }
    }
    return id;
}
static int memberships(const Metro *metro, int id) {
    int n = 0;
    for (size_t i = 0; i < metro->lines.rows.size; i++) if (contains(&metro->lines.rows.items[i].station_ids, id)) n++;
    return n;
}
static uint32_t dim_color(uint32_t rgb) {
    return (((rgb>>16)&255)/3<<16) | (((rgb>>8)&255)/3<<8) | ((rgb&255)/3);
}
static void draw_edge(BrailleCanvas *b, const Viewport *v, const MapDocument *map, const Edge *e, uint32_t rgb, int priority) {
    const MapStation *a = map_station(map, e->from_station_id), *z = map_station(map, e->to_station_id);
    if (!a || !z) return;
    bool found = false;
    for (size_t i = 1; i < map->vertices.size; i++) {
        const MapVertex *p = &map->vertices.items[i-1], *q = &map->vertices.items[i];
        if (p->edge_id != e->id || q->edge_id != e->id) continue;
        MapPoint x = viewport_project(v, p->point), y = viewport_project(v, q->point);
        braille_line(b, x.x, x.y, y.x, y.y, rgb, priority); found = true;
    }
    if (!found) {
        MapPoint x = viewport_project(v, a->point), y = viewport_project(v, z->point);
        braille_line(b, x.x, x.y, y.x, y.y, rgb, priority);
    }
}
static int label_priority(int id, int transfers, MapSelection sel) {
    if (id == sel.from || id == sel.to) return 0;
    if (id == sel.selected) return 1;
    bool on_route = sel.route && contains(&sel.route->stations, id);
    if (on_route && transfers > 1) return 2;
    if (on_route) return 3;
    return transfers > 1 ? 4 : 5;
}
int map_render(CellSurface *s, BrailleCanvas *b, const Viewport *v, const Metro *metro,
        const MapDocument *map, MapSelection sel, const BackgroundLayer *background) {
    if (braille_resize(b, v->cols, v->rows)) return -1;
    braille_clear(b);
    if (background && background->space == map->space && background->draw) background->draw(background->context, s, v);
    for (size_t i = 0; i < metro->edges.rows.size; i++) {
        const Edge *e = &metro->edges.rows.items[i];
        bool chosen = sel.route && contains(&sel.route->edges_ids, e->id);
        bool selected_line = false;
        const Line *l = line_find_by_id(&metro->lines, e->line_id);
        if (l) selected_line = contains(&l->station_ids, sel.selected);
        uint32_t rgb = map_color(map, e->line_id);
        if (sel.route && !chosen) rgb = dim_color(rgb);
        int priority = chosen ? INT_MAX : selected_line ? INT_MAX-1 : -e->line_id;
        draw_edge(b, v, map, e, rgb, priority);
    }
    braille_present(b, s, 0, 1);
    bool *occupied = calloc((size_t)v->cols*v->rows, sizeof(bool));
    if (!occupied) return -1;
    for (size_t i = 0; i < map->stations.size; i++) {
        const MapStation *st = &map->stations.items[i];
        MapPoint p = viewport_project(v, st->point);
        if (p.x < 0 || p.y < 0 || p.x >= v->cols*2 || p.y >= v->rows*4) continue;
        int x = (int)(p.x/2), y = (int)(p.y/4);
        uint32_t cp = st->station_id == sel.from ? 'S' : st->station_id == sel.to ? 'E' : memberships(metro, st->station_id) > 1 ? 0x25ce : 0x25cb;
        surface_put(s, x, y+1, cp, st->station_id == sel.selected ? 0xffffff : 0xd8dee9);
        occupied[y*v->cols+x] = true;
    }
    for (int priority = 0; priority < 6; priority++) for (size_t i = 0; i < map->stations.size; i++) {
        const MapStation *st = &map->stations.items[i];
        const Station *station = station_find_by_id(&metro->stations, st->station_id);
        if (!station || label_priority(st->station_id, memberships(metro, st->station_id), sel) != priority) continue;
        if (priority == 5 && v->zoom < v->fit_zoom*1.8) continue;
        MapPoint p = viewport_project(v, st->point);
        if (p.x < 0 || p.y < 0 || p.x >= v->cols*2 || p.y >= v->rows*4) continue;
        int width = render_display_width(station->name);
        int sx = (int)(p.x/2), sy = (int)(p.y/4);
        const int positions[5][2] = {{sx+st->label_dx-(st->label_left ? width : 0), sy+st->label_dy},
            {sx+2,sy}, {sx-width-2,sy}, {sx+2,sy-1}, {sx+2,sy+1}};
        for (int attempt = 0; attempt < 5; attempt++) {
            int x = positions[attempt][0], y = positions[attempt][1];
            if (x < 0 || y < 0 || x+width > v->cols || y >= v->rows) continue;
            bool free_space = true;
            for (int j = 0; j < width; j++) if (occupied[y*v->cols+x+j]) free_space = false;
            if (!free_space) continue;
            surface_text(s, x, y+1, width, station->name, 0xe5e9f0);
            for (int j = 0; j < width; j++) occupied[y*v->cols+x+j] = true;
            break;
        }
    }
    free(occupied); return 0;
}
