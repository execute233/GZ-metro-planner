#include "map_io.h"
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const char *names[] = {"map_stations.csv", "map_segments.csv", "map_styles.csv"};
static const char *headers[] = {"station_id,x,y,label_dx,label_dy,label_anchor", "edge_id,point_order,x,y", "line_id,color_rgb"};
static bool point_valid(MapPoint p) { return isfinite(p.x) && isfinite(p.y) && fabs(p.x) <= 1e7 && fabs(p.y) <= 1e7; }
static bool same_point(MapPoint a, MapPoint b) { return fabs(a.x-b.x) < 0.00001 && fabs(a.y-b.y) < 0.00001; }
static int fail(char *error, size_t cap, const char *name, size_t line, const char *reason) {
    snprintf(error, cap, "%s:%zu: %s", name, line, reason); return -1;
}
int map_io_validate(const Metro *metro, const MapDocument *map, char *error, size_t cap) {
    for (size_t i = 0; i < map->stations.size; i++) {
        const MapStation *s = &map->stations.items[i];
        if (!station_find_by_id(&metro->stations, s->station_id) || !point_valid(s->point) ||
            s->label_dx < -100 || s->label_dx > 100 || s->label_dy < -100 || s->label_dy > 100) return fail(error, cap, names[0], i+2, "站点引用或坐标非法");
        for (size_t j = 0; j < i; j++) if (map->stations.items[j].station_id == s->station_id) return fail(error, cap, names[0], i+2, "重复站点");
    }
    for (size_t i = 0; i < map->styles.size; i++) {
        if (!line_find_by_id(&metro->lines, map->styles.items[i].line_id)) return fail(error, cap, names[2], i+2, "线路不存在");
        for (size_t j = 0; j < i; j++) if (map->styles.items[j].line_id == map->styles.items[i].line_id) return fail(error, cap, names[2], i+2, "重复样式");
    }
    for (size_t i = 0; i < map->vertices.size;) {
        size_t start = i;
        int id = map->vertices.items[i].edge_id;
        const Edge *edge = edge_find_by_id(&metro->edges, id);
        if (!edge) return fail(error, cap, names[1], i+2, "区间不存在");
        const MapStation *a = map_station(map, edge->from_station_id), *b = map_station(map, edge->to_station_id);
        for (size_t j = 0; j < start; j++) if (map->vertices.items[j].edge_id == id) return fail(error, cap, names[1], i+2, "区间记录必须连续");
        while (i < map->vertices.size && map->vertices.items[i].edge_id == id) {
            if (map->vertices.items[i].order != (int)(i-start) || !point_valid(map->vertices.items[i].point)) return fail(error, cap, names[1], i+2, "折点顺序或坐标非法");
            i++;
        }
        if (i-start < 2 || !a || !b || !same_point(a->point, map->vertices.items[start].point) || !same_point(b->point, map->vertices.items[i-1].point))
            return fail(error, cap, names[1], start+2, "折线端点不匹配");
    }
    return 0;
}

static bool integer_field(const char **text, int *out, char delimiter) {
    char *end; errno = 0;
    long n = strtol(*text, &end, 10);
    if (errno || end == *text || n < -10000000 || n > 10000000 || *end != delimiter) return false;
    *out = (int)n; *text = end + (delimiter != 0); return true;
}
static bool real_field(const char **text, double *out, char delimiter) {
    char *end; errno = 0;
    double n = strtod(*text, &end);
    if (errno || end == *text || !isfinite(n) || fabs(n) > 1e7 || *end != delimiter) return false;
    *out = n; *text = end + (delimiter != 0); return true;
}
static int parse_record(MapDocument *map, int table, const char *line) {
    if (table == 0) {
        MapStation s = {0};
        if (!integer_field(&line, &s.station_id, ',') || !real_field(&line, &s.point.x, ',') || !real_field(&line, &s.point.y, ',') ||
            !integer_field(&line, &s.label_dx, ',') || !integer_field(&line, &s.label_dy, ',')) return -1;
        if (strcmp(line, "left") && strcmp(line, "right")) return -1;
        s.label_left = !strcmp(line, "left");
        return al_map_station_push(&map->stations, s);
    }
    if (table == 1) {
        MapVertex v;
        if (!integer_field(&line, &v.edge_id, ',') || !integer_field(&line, &v.order, ',') || !real_field(&line, &v.point.x, ',') || !real_field(&line, &v.point.y, 0)) return -1;
        return al_map_vertex_push(&map->vertices, v);
    }
    MapStyle s;
    if (!integer_field(&line, &s.line_id, ',') || *line != '#') return -1;
    const char *hash = line;
    if (!hash || strlen(hash+1) != 6 || strspn(hash+1, "0123456789abcdefABCDEF") != 6) return -1;
    s.rgb = (uint32_t)strtoul(hash+1, NULL, 16);
    return al_map_style_push(&map->styles, s);
}
int map_io_load(const char *dir, const Metro *metro, MapDocument *map, char *error, size_t cap) {
    MapDocument loaded; map_init(&loaded);
    for (int table = 0; table < 3; table++) {
        char path[1024], line[512];
        if (snprintf(path, sizeof(path), "%s/%s", dir, names[table]) >= (int)sizeof(path)) goto memory_fail;
        FILE *f = fopen(path, "rb");
        if (!f) { if (errno == ENOENT) continue; fail(error, cap, names[table], 0, "无法读取"); goto invalid; }
        size_t row = 0;
        while (fgets(line, sizeof(line), f)) {
            row++;
            size_t n = strlen(line);
            if (n == sizeof(line)-1 && line[n-1] != '\n') { fail(error, cap, names[table], row, "记录过长"); fclose(f); goto invalid; }
            while (n && (line[n-1] == '\r' || line[n-1] == '\n')) line[--n] = 0;
            if (row == 1) {
                if (strcmp(line, headers[table])) { fail(error, cap, names[table], row, "表头不匹配"); fclose(f); goto invalid; }
            } else if (!n || parse_record(&loaded, table, line)) { fail(error, cap, names[table], row, "记录格式或分配失败"); fclose(f); goto invalid; }
        }
        int bad = ferror(f) || row == 0;
        if (fclose(f)) bad = 1;
        if (bad) { fail(error, cap, names[table], row, "读取失败或空文件"); goto invalid; }
    }
    if (map_io_validate(metro, &loaded, error, cap)) goto invalid;
    map_dispose(map); *map = loaded;
    return 0;
memory_fail: snprintf(error, cap, "数据目录路径过长");
invalid: map_dispose(&loaded); return -1;
}
int map_io_write(FILE *f, const MapDocument *map, int table) {
    if (fprintf(f, "%s\n", headers[table]) < 0) return -1;
    if (table == 0) for (size_t i = 0; i < map->stations.size; i++) {
        const MapStation *s = &map->stations.items[i];
        if (fprintf(f, "%d,%.17g,%.17g,%d,%d,%s\n", s->station_id, s->point.x, s->point.y, s->label_dx, s->label_dy, s->label_left ? "left" : "right") < 0) return -1;
    }
    if (table == 1) for (size_t i = 0; i < map->vertices.size; i++) {
        const MapVertex *v = &map->vertices.items[i];
        if (fprintf(f, "%d,%d,%.17g,%.17g\n", v->edge_id, v->order, v->point.x, v->point.y) < 0) return -1;
    }
    if (table == 2) for (size_t i = 0; i < map->styles.size; i++) if (fprintf(f, "%d,#%06X\n", map->styles.items[i].line_id, map->styles.items[i].rgb) < 0) return -1;
    return ferror(f) ? -1 : 0;
}
