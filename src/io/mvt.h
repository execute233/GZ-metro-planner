#ifndef GZMP_MVT_H
#define GZMP_MVT_H
#include <stddef.h>
#include <stdint.h>
typedef struct {
    double x0, y0, x1, y1;
    int edge_id;
} MapSegment;
typedef struct {
    MapSegment *items;
    size_t count, capacity;
} MapSegments;
/* Decode MVT v2 edges layer, coordinates in the 4096-square schematic.
 * out must be zero-initialized or contain a previous decode result.
 * On failure out is empty. Caller owns out and must dispose it. */
int mvt_decode(const uint8_t *bytes, size_t size, int z, int x, int y, MapSegments *out);
/* Accept gzip-compressed or raw tile bytes. */
int mvt_decode_blob(const uint8_t *bytes, size_t size, int z, int x, int y, MapSegments *out);
void mvt_dispose(MapSegments *segments);
#endif
