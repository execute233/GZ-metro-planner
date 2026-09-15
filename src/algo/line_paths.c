#include "line_paths.h"
#include "../io/metro_io.h"
#include <stdlib.h>

int line_paths(const Metro *metro, int line_id, ArrayList_Int *out) {
    const ArrayList_Edge *edges = &metro->edges.rows;
    int degree[METRO_STATION_LIMIT] = {0};
    unsigned char *used = calloc(edges->size ? edges->size : 1, 1);
    if (!used)
        return -1;
    for (size_t i = 0; i < edges->size; i++) {
        const Edge *e = &edges->items[i];
        if (e->line_id == line_id) {
            degree[e->from_station_id]++;
            degree[e->to_station_id]++;
        }
    }
    /* First walk from endpoints/junctions, then handle remaining pure cycles. */
    for (int pass = 0; pass < 2; pass++) {
        for (size_t i = 0; i < edges->size; i++) {
            const Edge *e = &edges->items[i];
            if (used[i] || e->line_id != line_id)
                continue;
            int start = e->from_station_id;
            if (!pass && degree[start] == 2) {
                start = e->to_station_id;
                if (degree[start] == 2)
                    continue;
            }
            if ((out->size && al_int_push(out, 0)) || al_int_push(out, start))
                goto failed;
            size_t next = i;
            int station = start;
            while (next < edges->size) {
                e = &edges->items[next];
                used[next] = 1;
                station = e->from_station_id == station ? e->to_station_id : e->from_station_id;
                if (al_int_push(out, station))
                    goto failed;
                if (station == start || degree[station] != 2)
                    break;
                for (next = 0; next < edges->size; next++) {
                    e = &edges->items[next];
                    if (!used[next] && e->line_id == line_id &&
                        (e->from_station_id == station || e->to_station_id == station))
                        break;
                }
            }
        }
    }
    free(used);
    return 0;
failed:
    free(used);
    al_int_dispose(out);
    return -1;
}
