#include "tile_writer.h"
#include "metro_io.h"
#include "mvt.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

typedef struct {
    unsigned char *bytes;
    size_t size, capacity;
} Buffer;

static int put(Buffer *b, const void *bytes, size_t size) {
    if (size > 4 * 1024 * 1024 - b->size)
        return -1;
    if (b->size + size > b->capacity) {
        size_t capacity = (b->size + size) * 2;
        void *p = realloc(b->bytes, capacity);
        if (!p)
            return -1;
        b->bytes = p;
        b->capacity = capacity;
    }
    memcpy(b->bytes + b->size, bytes, size);
    b->size += size;
    return 0;
}

static int number(Buffer *b, unsigned value) {
    unsigned char bytes[5];
    size_t size = 0;
    do {
        bytes[size++] = (value & 127) | (value > 127 ? 128 : 0);
        value >>= 7;
    } while (value);
    return put(b, bytes, size);
}

static int message(Buffer *b, unsigned field, const void *bytes, size_t size) {
    return number(b, field * 8 + 2) || number(b, (unsigned)size) || put(b, bytes, size);
}

static unsigned zigzag(int value) {
    return value < 0 ? (unsigned)(-value) * 2 - 1 : (unsigned)value * 2;
}

static int bind_gzip(sqlite3_stmt *query, const Buffer *tile) {
    z_stream stream = {0};
    if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 31, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return -1;
    size_t capacity = deflateBound(&stream, (uLong)tile->size);
    unsigned char *compressed = malloc(capacity);
    int result = -1;
    if (compressed) {
        stream.next_in = tile->bytes;
        stream.avail_in = (unsigned)tile->size;
        stream.next_out = compressed;
        stream.avail_out = (unsigned)capacity;
        if (deflate(&stream, Z_FINISH) == Z_STREAM_END &&
            sqlite3_bind_blob(query, 4, compressed, (int)stream.total_out, SQLITE_TRANSIENT) == SQLITE_OK)
            result = 0;
    }
    deflateEnd(&stream);
    free(compressed);
    return result;
}

/* Liang-Barsky clipping in schematic coordinates. */
static int clip(MapSegment *s, double left, double top, double size) {
    double dx = s->x1 - s->x0, dy = s->y1 - s->y0;
    double p[] = {-dx, dx, -dy, dy};
    double q[] = {s->x0 - left, left + size - s->x0,
                  s->y0 - top, top + size - s->y0};
    double low = 0, high = 1;
    for (int i = 0; i < 4; i++) {
        if (p[i] == 0) {
            if (q[i] < 0)
                return 0;
        } else {
            double t = q[i] / p[i];
            if (p[i] < 0)
                low = fmax(low, t);
            else
                high = fmin(high, t);
        }
    }
    if (low >= high)
        return 0;
    s->x1 = s->x0 + high * dx;
    s->y1 = s->y0 + high * dy;
    s->x0 += low * dx;
    s->y0 += low * dy;
    return 1;
}

static int encode(const MapSegments *segments, const Metro *metro, int z, int x, int y,
                  Buffer *tile) {
    Buffer layer = {0}, feature = {0}, geometry = {0};
    int result = -1;
    if (message(&layer, 1, "edges", 5) || number(&layer, 40) || number(&layer, 4096) ||
        number(&layer, 120) || number(&layer, 2) || message(&layer, 3, "line_id", 7))
        goto done;
    /* Value index equals line id; the model bounds ids below 128. */
    for (unsigned i = 0; i < METRO_LINE_LIMIT; i++) {
        unsigned char value[] = {40, (unsigned char)i};
        if (message(&layer, 4, value, sizeof(value)))
            goto done;
    }
    for (size_t i = 0; i < segments->count; i++) {
        MapSegment s = segments->items[i];
        const Edge *edge = edge_find_by_id(&metro->edges, s.edge_id);
        if (!edge)
            goto done;
        int x0 = (int)lround(s.x0 * (1 << z) - x * 4096);
        int y0 = (int)lround(s.y0 * (1 << z) - y * 4096);
        int x1 = (int)lround(s.x1 * (1 << z) - x * 4096);
        int y1 = (int)lround(s.y1 * (1 << z) - y * 4096);
        geometry.size = feature.size = 0;
        unsigned char tags[] = {0, (unsigned char)edge->line_id};
        if (number(&geometry, 9) || number(&geometry, zigzag(x0)) ||
            number(&geometry, zigzag(y0)) || number(&geometry, 10) ||
            number(&geometry, zigzag(x1 - x0)) || number(&geometry, zigzag(y1 - y0)) ||
            number(&feature, 8) || number(&feature, (unsigned)edge->id) ||
            message(&feature, 2, tags, sizeof(tags)) || number(&feature, 24) ||
            number(&feature, 2) || message(&feature, 4, geometry.bytes, geometry.size) ||
            message(&layer, 2, feature.bytes, feature.size))
            goto done;
    }
    result = message(tile, 3, layer.bytes, layer.size) ? -1 : 0;
done:
    free(layer.bytes);
    free(feature.bytes);
    free(geometry.bytes);
    return result;
}

static int same_geometry(const Metro *before, const Metro *after, const Edge *edge) {
    const Edge *old = edge_find_by_id(&before->edges, edge->id);
    if (!old || old->line_id != edge->line_id || old->from_station_id != edge->from_station_id ||
        old->to_station_id != edge->to_station_id)
        return 0;
    const int ids[] = {edge->from_station_id, edge->to_station_id};
    for (int i = 0; i < 2; i++) {
        const Station *a = station_find_by_id(&before->stations, ids[i]);
        const Station *b = station_find_by_id(&after->stations, ids[i]);
        if (!a || !b || a->x != b->x || a->y != b->y)
            return 0;
    }
    return 1;
}

int tile_writer_update(sqlite3 *db, const Metro *before, const Metro *after) {
    unsigned char keep[METRO_EDGE_LIMIT] = {0};
    int changed = before->edges.rows.size != after->edges.rows.size;
    for (size_t i = 0; i < after->edges.rows.size; i++) {
        const Edge *edge = &after->edges.rows.items[i];
        keep[edge->id] = (unsigned char)same_geometry(before, after, edge);
        changed |= !keep[edge->id];
    }
    if (!changed)
        return 0;
    sqlite3_stmt *read = NULL, *write = NULL, *drop = NULL;
    MapSegments segments = {0};
    Buffer tile = {0};
    int result = -1;
    /* Maintenance supports the generator's bounded zoom range, 0 through 5. */
    if (sqlite3_prepare_v2(db, "SELECT value FROM metadata WHERE name='maxzoom'", -1, &read, NULL) != SQLITE_OK ||
        sqlite3_step(read) != SQLITE_ROW)
        goto done;
    int maxzoom = sqlite3_column_int(read, 0);
    sqlite3_finalize(read);
    read = NULL;
    if (maxzoom < 0 || maxzoom > 5)
        goto done;
    if (sqlite3_prepare_v2(db, "SELECT tile_data FROM tiles WHERE zoom_level=? AND tile_column=? AND tile_row=?",
                           -1, &read, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO tiles VALUES(?,?,?,?)", -1, &write, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(db, "DELETE FROM tiles WHERE zoom_level=? AND tile_column=? AND tile_row=?",
                           -1, &drop, NULL) != SQLITE_OK)
        goto done;
    for (int z = 0; z <= maxzoom; z++) {
        int n = 1 << z;
        for (int y = 0; y < n; y++) {
            for (int x = 0; x < n; x++) {
                sqlite3_bind_int(read, 1, z);
                sqlite3_bind_int(read, 2, x);
                sqlite3_bind_int(read, 3, n - 1 - y);
                int rc = sqlite3_step(read), dirty = 0;
                mvt_dispose(&segments);
                if (rc == SQLITE_ROW) {
                    if (mvt_decode_blob(sqlite3_column_blob(read, 0),
                                        (size_t)sqlite3_column_bytes(read, 0), z, x, y, &segments))
                        goto done;
                } else if (rc != SQLITE_DONE)
                    goto done;
                sqlite3_reset(read);
                size_t count = 0;
                for (size_t i = 0; i < segments.count; i++) {
                    int id = segments.items[i].edge_id;
                    if (id > 0 && id < METRO_EDGE_LIMIT && keep[id])
                        segments.items[count++] = segments.items[i];
                    else
                        dirty = 1;
                }
                segments.count = count;
                for (size_t i = 0; i < after->edges.rows.size; i++) {
                    const Edge *edge = &after->edges.rows.items[i];
                    if (keep[edge->id])
                        continue;
                    const Station *a = station_find_by_id(&after->stations, edge->from_station_id);
                    const Station *b = station_find_by_id(&after->stations, edge->to_station_id);
                    MapSegment s = {a->x, a->y, b->x, b->y, edge->id};
                    if (!clip(&s, x * 4096.0 / n, y * 4096.0 / n, 4096.0 / n))
                        continue;
                    void *p = realloc(segments.items, (segments.count + 1) * sizeof(s));
                    if (!p)
                        goto done;
                    segments.items = p;
                    segments.items[segments.count++] = s;
                    dirty = 1;
                }
                if (!dirty)
                    continue;
                sqlite3_stmt *q = segments.count || z == 0 ? write : drop;
                sqlite3_bind_int(q, 1, z);
                sqlite3_bind_int(q, 2, x);
                sqlite3_bind_int(q, 3, n - 1 - y);
                if (q == write) {
                    tile.size = 0;
                    if (encode(&segments, after, z, x, y, &tile) || bind_gzip(q, &tile))
                        goto done;
                }
                if (sqlite3_step(q) != SQLITE_DONE)
                    goto done;
                sqlite3_reset(q);
            }
        }
    }
    result = 0;
done:
    sqlite3_finalize(read);
    sqlite3_finalize(write);
    sqlite3_finalize(drop);
    mvt_dispose(&segments);
    free(tile.bytes);
    return result;
}
