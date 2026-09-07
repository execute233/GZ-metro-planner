#include "graph.h"

#include <stdlib.h>
#include <string.h>

static int has_id(const ArrayList_Int *list, int id) {
    for (size_t i = 0; i < list->size; i++) {
        if (list->items[i] == id)
            return 1;
    }
    return 0;
}

int graph_build(Graph *g, const Metro *metro) {
    int max_id = 0;
    for (size_t i = 0; i < metro->stations.rows.size; i++) {
        int id = metro->stations.rows.items[i].id;
        if (id > max_id)
            max_id = id;
    }
    g->capacity = max_id + 1;
    g->adj = calloc((size_t)g->capacity, sizeof(ArrayList_Int));
    if (g->adj == NULL)
        return -1;
    for (int i = 0; i < g->capacity; i++)
        al_int_init(&g->adj[i]);

    for (size_t i = 0; i < metro->edges.rows.size; i++) {
        const Edge *e = &metro->edges.rows.items[i];
        if (e->from_station_id >= g->capacity || e->to_station_id >= g->capacity)
            continue;
        if (!has_id(&g->adj[e->from_station_id], e->to_station_id))
            al_int_push(&g->adj[e->from_station_id], e->to_station_id);
        if (!has_id(&g->adj[e->to_station_id], e->from_station_id))
            al_int_push(&g->adj[e->to_station_id], e->from_station_id);
    }
    return 0;
}

void graph_dispose(Graph *g) {
    if (g->adj == NULL)
        return;
    for (int i = 0; i < g->capacity; i++)
        al_int_dispose(&g->adj[i]);
    free(g->adj);
    g->adj = NULL;
    g->capacity = 0;
}

const ArrayList_Int *graph_neighbors(const Graph *g, int station_id) {
    if (station_id < 0 || station_id >= g->capacity)
        return NULL;
    return &g->adj[station_id];
}