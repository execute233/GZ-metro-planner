#include "map_render.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

int frame_resize(MapFrame *f, int w, int h) {
    if (w < 1 || h < 1 || w > 1000 || h > 500)
        return -1;
    if (w == f->width && h == f->height)
        return 0;
    MapCell *p = calloc((size_t)w * h, sizeof(*p));
    if (!p)
        return -1;
    free(f->cells);
    f->cells = p;
    f->width = w;
    f->height = h;
    return 0;
}
void frame_clear(MapFrame *f) {
    memset(f->cells, 0, (size_t)f->width * f->height * sizeof(*f->cells));
}
void frame_dispose(MapFrame *f) {
    free(f->cells);
    memset(f, 0, sizeof(*f));
}
int utf8_next(const char **s, uint32_t *cp) {
    const unsigned char *p = (const unsigned char *)*s;
    if (!*p)
        return 0;
    unsigned n = 1;
    uint32_t v = *p;
    if (*p >= 0xc2 && *p <= 0xdf) {
        n = 2;
        v = *p & 31;
    } else if (*p >= 0xe0 && *p <= 0xef) {
        n = 3;
        v = *p & 15;
    } else if (*p >= 0xf0 && *p <= 0xf4) {
        n = 4;
        v = *p & 7;
    } else if (*p >= 0x80)
        goto invalid;
    for (unsigned i = 1; i < n; i++) {
        if ((p[i] & 0xc0) != 0x80)
            goto invalid;
        v = (v << 6) | (p[i] & 63);
    }
    if ((n == 3 && v < 0x800) || (n == 4 && v < 0x10000) || v > 0x10ffff ||
        (v >= 0xd800 && v <= 0xdfff))
        goto invalid;
    *s += n;
    *cp = v;
    return 1;
invalid:
    (*s)++;
    *cp = 0xfffd;
    return 1;
}
int unicode_width(uint32_t c) {
    if (c < 32 || (c >= 0x7f && c < 0xa0) || (c >= 0x300 && c <= 0x36f) ||
        (c >= 0xfe00 && c <= 0xfe0f))
        return 0;
    return ((c >= 0x1100 && c <= 0x115f) || (c >= 0x2e80 && c <= 0xa4cf) ||
            (c >= 0xac00 && c <= 0xd7a3) || (c >= 0xf900 && c <= 0xfaff) ||
            (c >= 0xfe10 && c <= 0xfe6f) || (c >= 0xff01 && c <= 0xff60) || c >= 0x1f300)
               ? 2
               : 1;
}
unsigned map_display_color(unsigned rgb) {
    /* Lift dark source colors on the terminal's dark background; keep hue. */
    double r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
    double light = .2126 * r + .7152 * g + .0722 * b;
    if (light < 125) {
        double t = (125 - light) / (255 - light);
        r += (255 - r) * t;
        g += (255 - g) * t;
        b += (255 - b) * t;
    }
    return ((unsigned)r << 16) | ((unsigned)g << 8) | (unsigned)b;
}
void frame_glyph(MapFrame *f, int x, int y, uint32_t cp, int color, int dim) {
    int w = unicode_width(cp);
    if (x < 0 || y < 0 || y >= f->height || !w || x + w > f->width)
        return;
    MapCell *c = &f->cells[y * f->width + x];
    if (c->continuation && x)
        memset(c - 1, 0, sizeof(*c));
    if (unicode_width(c->glyph) == 2 && x + 1 < f->width)
        memset(c + 1, 0, sizeof(*c));
    if (w == 2 && x + 2 < f->width && unicode_width(c[1].glyph) == 2)
        memset(c + 2, 0, sizeof(*c));
    *c = (MapCell){cp, (short)color, (unsigned char)dim, 0};
    if (w == 2)
        c[1] = (MapCell){0, (short)color, (unsigned char)dim, 1};
}
void frame_text(MapFrame *f, int x, int y, int maxw, const char *s, int color, int dim) {
    uint32_t cp;
    int end = x + maxw;
    while (utf8_next(&s, &cp)) {
        int w = unicode_width(cp);
        if (!w)
            continue;
        if (x + w > end)
            break;
        frame_glyph(f, x, y, cp, color, dim);
        x += w;
    }
}
void viewport_fit(Viewport *v, const MapDb *db, const Route *route, int cols, int rows) {
    double x0 = 4096, y0 = 4096, x1 = 0, y1 = 0;
    size_t n = route ? route->stations.size : db->metro.stations.rows.size;
    if (!n || cols < 1 || rows < 1)
        return;
    for (size_t i = 0; i < n; i++) {
        int id = route ? route->stations.items[i] : db->metro.stations.rows.items[i].id;
        MapStation s = db->stations[id];
        x0 = fmin(x0, s.x);
        y0 = fmin(y0, s.y);
        x1 = fmax(x1, s.x);
        y1 = fmax(y1, s.y);
    }
    v->x = (x0 + x1) / 2;
    v->y = (y0 + y1) / 2;
    v->scale =
        fmin(cols * 2.0 / fmax(x1 - x0 + 120, 300), rows * 4.0 / fmax(y1 - y0 + 120, 300)) * .85;
    v->scale = fmax(.015, fmin(v->scale, 2.0));
}
void viewport_zoom(Viewport *v, double factor, int cols, int rows, double ax, double ay) {
    double wx = v->x + (ax - cols) / v->scale, wy = v->y + (ay - rows * 2) / v->scale;
    v->scale = fmax(.015, fmin(2.0, v->scale * factor));
    v->x = wx - (ax - cols) / v->scale;
    v->y = wy - (ay - rows * 2) / v->scale;
}
static int selected(const Route *r, int id) {
    if (r)
        for (size_t i = 0; i < r->edges_ids.size; i++)
            if (r->edges_ids.items[i] == id)
                return 1;
    return 0;
}
static int clip(double p, double q, double *a, double *b) {
    if (p == 0)
        return q >= 0;
    double t = q / p;
    if (p < 0) {
        if (t > *b)
            return 0;
        if (t > *a)
            *a = t;
    } else {
        if (t < *a)
            return 0;
        if (t < *b)
            *b = t;
    }
    return 1;
}
static void dot(MapFrame *f, int px, int py, int cols, int rows, int color, int dim) {
    static const unsigned bits[4][2] = {{1, 8}, {2, 16}, {4, 32}, {64, 128}};
    if (px < 0 || py < 0 || px >= cols * 2 || py >= rows * 4)
        return;
    MapCell *c = &f->cells[(py / 4) * f->width + px / 2];
    unsigned mask = (c->glyph >= 0x2800 && c->glyph <= 0x28ff) ? c->glyph - 0x2800 : 0;
    if (!dim && c->dim)
        mask = 0;
    c->glyph = 0x2800 + (mask | bits[py % 4][px % 2]);
    c->color = (short)color;
    c->dim = (unsigned char)dim;
}
static void line(MapFrame *f, Viewport v, MapSegment s, int cols, int rows, int color, int dim) {
    double x = (s.x0 - v.x) * v.scale + cols, y = (s.y0 - v.y) * v.scale + rows * 2;
    double dx = (s.x1 - s.x0) * v.scale, dy = (s.y1 - s.y0) * v.scale, a = 0, b = 1;
    if (!clip(-dx, x, &a, &b) || !clip(dx, cols * 2 - 1 - x, &a, &b) || !clip(-dy, y, &a, &b) ||
        !clip(dy, rows * 4 - 1 - y, &a, &b))
        return;
    int x0 = (int)lround(x + a * dx), y0 = (int)lround(y + a * dy), x1 = (int)lround(x + b * dx),
        y1 = (int)lround(y + b * dy);
    int xx = abs(x1 - x0), yy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1,
        err = xx + yy;
    for (;;) {
        dot(f, x0, y0, cols, rows, color, dim);
        if (x0 == x1 && y0 == y1)
            break;
        int e = 2 * err;
        if (e >= yy) {
            err += yy;
            x0 += sx;
        }
        if (e <= xx) {
            err += xx;
            y0 += sy;
        }
    }
}
int map_render(MapFrame *f, MapDb *db, Viewport v, const Route *r, int from, int to, int cols,
               int rows) {
    if (cols < 1 || rows < 1 || cols > f->width || rows > f->height || !isfinite(v.scale) ||
        v.scale <= 0)
        return -1;
    int z = (int)floor(log2(v.scale * 16));
    z = z < db->minzoom ? db->minzoom : z > db->maxzoom ? db->maxzoom : z;
    double tile = 4096.0 / (1 << z);
    int xa = (int)floor((v.x - cols / v.scale) / tile),
        xb = (int)floor((v.x + cols / v.scale) / tile);
    int ya = (int)floor((v.y - rows * 2 / v.scale) / tile),
        yb = (int)floor((v.y + rows * 2 / v.scale) / tile);
    xa = xa < 0 ? 0 : xa;
    ya = ya < 0 ? 0 : ya;
    xb = xb >= (1 << z) ? (1 << z) - 1 : xb;
    yb = yb >= (1 << z) ? (1 << z) - 1 : yb;
    int errors = 0;
    for (int pass = 0; pass < (r ? 2 : 1); pass++)
        for (int y = ya; y <= yb; y++)
            for (int x = xa; x <= xb; x++) {
                const MapSegments *segs;
                int status = map_db_tile(db, z, x, y, &segs);
                if (status < 0) {
                    if (!pass)
                        errors++;
                    continue;
                }
                if (status > 0)
                    continue;
                for (size_t i = 0; i < segs->count; i++) {
                    MapSegment s = segs->items[i];
                    int sel = selected(r, s.edge_id);
                    if (r && sel != pass)
                        continue;
                    const Edge *e = edge_find_by_id(&db->metro.edges, s.edge_id);
                    if (!e)
                        continue;
                    line(f, v, s, cols, rows, 4 + e->line_id, r && !sel);
                }
            }
    unsigned char *occupied = calloc((size_t)cols * rows, 1);
    if (!occupied)
        return -1;
    for (size_t i = 0; i < db->metro.stations.rows.size; i++) {
        int id = db->metro.stations.rows.items[i].id;
        MapStation p = db->stations[id];
        int x = (int)floor(((p.x - v.x) * v.scale + cols) / 2),
            y = (int)floor(((p.y - v.y) * v.scale + rows * 2) / 4);
        if (x < 0 || y < 0 || x >= cols || y >= rows)
            continue;
        int endpoint = id == from || id == to;
        if (v.scale > .065 || endpoint) {
            frame_glyph(f, x, y,
                        endpoint     ? '@'
                        : p.transfer ? 0x25ce
                                     : 0x00b7,
                        endpoint ? 1 : 0, 0);
            occupied[y * cols + x] = 1;
        }
    }
    for (int priority = 0; priority < 3; priority++)
        for (size_t i = 0; i < db->metro.stations.rows.size; i++) {
            const Station *st = &db->metro.stations.rows.items[i];
            MapStation p = db->stations[st->id];
            int rank = (st->id == from || st->id == to) ? 0 : p.transfer ? 1 : 2;
            if (rank != priority || (rank == 1 && v.scale < .07) || (rank == 2 && v.scale < .18))
                continue;
            int x = (int)floor(((p.x - v.x) * v.scale + cols) / 2),
                y = (int)floor(((p.y - v.y) * v.scale + rows * 2) / 4);
            if (x < 0 || y < 0 || x >= cols || y >= rows)
                continue;
            int width = 0;
            uint32_t cp;
            const char *s = st->name;
            while (utf8_next(&s, &cp))
                width += unicode_width(cp);
            int offsets[4][2] = {{1, 0}, {-width - 1, 0}, {1, -1}, {1, 1}};
            for (int k = 0; k < 4; k++) {
                int a = x + offsets[k][0], b = y + offsets[k][1];
                if (a < 0 || b < 0 || a + width > cols || b >= rows)
                    continue;
                int ok = 1;
                for (int t = 0; t < width; t++)
                    if (occupied[b * cols + a + t])
                        ok = 0;
                if (!ok)
                    continue;
                frame_text(f, a, b, width, st->name, rank == 0 ? 1 : 0, 0);
                for (int t = 0; t < width; t++)
                    occupied[b * cols + a + t] = 1;
                break;
            }
        }
    free(occupied);
    return errors;
}
