#include "mvt.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

typedef struct {
    const uint8_t *p, *end;
    int bad;
} Reader;
static uint64_t varint(Reader *r) {
    uint64_t v = 0;
    for (int shift = 0; shift < 70; shift += 7) {
        if (r->p == r->end)
            break;
        unsigned b = *r->p++;
        if (shift == 63 && b > 1)
            break;
        v |= (uint64_t)(b & 127) << shift;
        if (!(b & 128))
            return v;
    }
    r->bad = 1;
    return 0;
}
static Reader message(Reader *r) {
    uint64_t n = varint(r);
    if (r->bad || n > (uint64_t)(r->end - r->p)) {
        r->bad = 1;
        return (Reader){r->end, r->end, 1};
    }
    Reader sub = {r->p, r->p + (size_t)n, 0};
    r->p = sub.end;
    return sub;
}
static void skip(Reader *r, unsigned wire) {
    size_t n = 0;
    if (wire == 0) {
        (void)varint(r);
        return;
    }
    if (wire == 2) {
        (void)message(r);
        return;
    }
    if (wire == 1)
        n = 8;
    else if (wire == 5)
        n = 4;
    else {
        r->bad = 1;
        return;
    }
    if ((size_t)(r->end - r->p) < n)
        r->bad = 1;
    else
        r->p += n;
}
static int append(MapSegments *s, MapSegment v) {
    if (s->count >= 100000)
        return -1;
    if (s->count == s->capacity) {
        size_t n = s->capacity ? s->capacity * 2 : 128;
        MapSegment *p = realloc(s->items, n * sizeof(*p));
        if (!p)
            return -1;
        s->items = p;
        s->capacity = n;
    }
    s->items[s->count++] = v;
    return 0;
}
static int64_t unzigzag(uint64_t value) {
    return (value & 1) ? -(int64_t)(value >> 1) - 1 : (int64_t)(value >> 1);
}
static int geometry(Reader r, int id, unsigned extent, int z, int tx, int ty, MapSegments *out) {
    int64_t x = 0, y = 0;
    int have = 0;
    double scale = 4096.0 / (1u << z), unit = scale / extent;
    while (r.p < r.end && !r.bad) {
        uint64_t cmd = varint(&r), count = cmd >> 3, op = cmd & 7;
        if (!count || count > 100000 || (op != 1 && op != 2) ||
            (op == 1 && (count != 1 || have == 1)))
            return -1;
        if (op == 2 && !have)
            return -1;
        for (uint64_t i = 0; i < count; i++) {
            uint64_t a = varint(&r), b = varint(&r);
            if (r.bad || a > UINT32_MAX || b > UINT32_MAX)
                return -1;
            int64_t nx = x + unzigzag(a);
            int64_t ny = y + unzigzag(b);
            if (llabs(nx) > 16777216 || llabs(ny) > 16777216)
                return -1;
            if (op == 2 &&
                append(out, (MapSegment){tx * scale + x * unit, ty * scale + y * unit,
                                         tx * scale + nx * unit, ty * scale + ny * unit, id}))
                return -1;
            x = nx;
            y = ny;
            have = (int)op;
        }
    }
    return r.bad || have != 2 ? -1 : 0;
}
static int feature(Reader r, unsigned extent, int z, int x, int y, MapSegments *out) {
    uint64_t id = 0, type = 0;
    Reader geom = {r.end, r.end, 0};
    while (r.p < r.end && !r.bad) {
        uint64_t tag = varint(&r);
        unsigned field = (unsigned)(tag >> 3), wire = tag & 7;
        if (!field)
            return -1;
        if (field == 1 && wire == 0)
            id = varint(&r);
        else if (field == 3 && wire == 0)
            type = varint(&r);
        else if (field == 4 && wire == 2)
            geom = message(&r);
        else
            skip(&r, wire);
    }
    if (r.bad || !id || id > INT_MAX || type != 2)
        return -1;
    return geometry(geom, (int)id, extent, z, x, y, out);
}
static int layer(Reader r, int z, int x, int y, MapSegments *out) {
    Reader start = r;
    unsigned extent = 4096;
    uint64_t version = 0;
    int edges = 0;
    while (r.p < r.end && !r.bad) {
        uint64_t tag = varint(&r);
        unsigned f = (unsigned)(tag >> 3), w = tag & 7;
        if (!f)
            return -1;
        if (f == 1 && w == 2) {
            Reader name = message(&r);
            edges = name.end - name.p == 5 && !memcmp(name.p, "edges", 5);
        } else if (f == 5 && w == 0) {
            uint64_t e = varint(&r);
            if (!e || e > 65536)
                return -1;
            extent = (unsigned)e;
        } else if (f == 15 && w == 0)
            version = varint(&r);
        else
            skip(&r, w);
    }
    if (r.bad)
        return -1;
    if (!edges)
        return 0;
    if (version != 2)
        return -1;
    r = start;
    while (r.p < r.end && !r.bad) {
        uint64_t tag = varint(&r);
        if ((tag >> 3) == 2 && (tag & 7) == 2) {
            Reader sub = message(&r);
            if (r.bad || feature(sub, extent, z, x, y, out))
                return -1;
        } else
            skip(&r, tag & 7);
    }
    return r.bad ? -1 : 0;
}
void mvt_dispose(MapSegments *s) {
    free(s->items);
    memset(s, 0, sizeof(*s));
}
int mvt_decode(const uint8_t *bytes, size_t size, int z, int x, int y, MapSegments *out) {
    mvt_dispose(out);
    if (!bytes || size > 4 * 1024 * 1024 || z < 0 || z > 14 || x < 0 || y < 0 || x >= (1 << z) ||
        y >= (1 << z))
        return -1;
    Reader r = {bytes, bytes + size, 0};
    while (r.p < r.end && !r.bad) {
        uint64_t tag = varint(&r);
        if (!(tag >> 3)) {
            r.bad = 1;
            break;
        }
        if ((tag >> 3) == 3 && (tag & 7) == 2) {
            Reader sub = message(&r);
            if (r.bad || layer(sub, z, x, y, out)) {
                r.bad = 1;
                break;
            }
        } else
            skip(&r, tag & 7);
    }
    if (r.bad) {
        mvt_dispose(out);
        return -1;
    }
    return 0;
}

int mvt_decode_blob(const uint8_t *bytes, size_t size, int z, int x, int y, MapSegments *out) {
    mvt_dispose(out);
    if (!bytes || !size || size > 4 * 1024 * 1024)
        return -1;
    if (size < 2 || bytes[0] != 31 || bytes[1] != 139)
        return mvt_decode(bytes, size, z, x, y, out);
    uint8_t *raw = malloc(4 * 1024 * 1024);
    if (!raw)
        return -1;
    z_stream stream = {0};
    stream.next_in = (Bytef *)bytes;
    stream.avail_in = (unsigned)size;
    stream.next_out = raw;
    stream.avail_out = 4 * 1024 * 1024;
    int result = -1;
    if (inflateInit2(&stream, 31) == Z_OK) {
        if (inflate(&stream, Z_FINISH) == Z_STREAM_END && !stream.avail_in)
            result = mvt_decode(raw, stream.total_out, z, x, y, out);
        inflateEnd(&stream);
    }
    free(raw);
    return result;
}
