#include "router.h"

#include <stdlib.h>
#include <string.h>

#define INF 1000000000

int route_init(Route *route) {
    al_int_init(&route->stations);
    al_int_init(&route->edges_ids);
    al_int_init(&route->transfers);
    route->total_stations = 0;
    route->total_meters = 0;
    route->total_seconds = 0;
    return 0;
}

void route_dispose(Route *route) {
    al_int_dispose(&route->stations);
    al_int_dispose(&route->edges_ids);
    al_int_dispose(&route->transfers);
}

/* 取 (a,b) 之间的边权；无边时返回 1（不应发生，图由边构建） */
static int edge_weight(const EdgeTable *edges, int a, int b, RouteMetric metric) {
    ArrayList_Int out;
    al_int_init(&out);
    edge_find_between(edges, a, b, &out);
    int w = 1;
    if (out.size > 0) {
        const Edge *e = edge_find_by_id(edges, out.items[0]);
        w = (metric == ROUTE_MIN_DISTANCE) ? e->cost_meters : e->cost_time_second;
    }
    al_int_dispose(&out);
    return w;
}

static int bfs(const Graph *g, int from, int to, int *prev) {
    ArrayList_Int queue;
    al_int_init(&queue);
    al_int_push(&queue, from);
    prev[from] = from;
    size_t head = 0;
    while (head < queue.size) {
        int cur = queue.items[head++];
        if (cur == to) {
            al_int_dispose(&queue);
            return 1;
        }
        const ArrayList_Int *nb = graph_neighbors(g, cur);
        for (size_t i = 0; i < nb->size; i++) {
            int v = nb->items[i];
            if (prev[v] == -1) {
                prev[v] = cur;
                al_int_push(&queue, v);
            }
        }
    }
    al_int_dispose(&queue);
    return 0;
}

static int dijkstra(const Graph *g, const EdgeTable *edges, int from, int to,
                    RouteMetric metric, int *prev) {
    int n = g->capacity;
    int *dist = malloc((size_t)n * sizeof(int));
    char *done = calloc((size_t)n, sizeof(char));
    if (dist == NULL || done == NULL) {
        free(dist);
        free(done);
        return 0;
    }
    for (int i = 0; i < n; i++) {
        dist[i] = INF;
        prev[i] = -1;
    }
    dist[from] = 0;

    for (;;) {
        int u = -1;
        int best = INF;
        for (int i = 0; i < n; i++) {
            if (!done[i] && dist[i] < best) {
                best = dist[i];
                u = i;
            }
        }
        if (u == -1 || u == to)
            break;
        done[u] = 1;
        const ArrayList_Int *nb = graph_neighbors(g, u);
        for (size_t i = 0; i < nb->size; i++) {
            int v = nb->items[i];
            if (done[v])
                continue;
            int w = edge_weight(edges, u, v, metric);
            if (dist[u] + w < dist[v]) {
                dist[v] = dist[u] + w;
                prev[v] = u;
            }
        }
    }
    int found = (prev[to] != -1);
    free(dist);
    free(done);
    return found;
}

/* 选 (a,b) 之间的边：共线段优先延续上一段所在线路 */
static int pick_edge(const EdgeTable *edges, int a, int b, int prev_line) {
    ArrayList_Int out;
    al_int_init(&out);
    edge_find_between(edges, a, b, &out);
    int chosen = out.size > 0 ? out.items[0] : -1;
    for (size_t i = 0; i < out.size; i++) {
        const Edge *e = edge_find_by_id(edges, out.items[i]);
        if (e != NULL && e->line_id == prev_line) {
            chosen = out.items[i];
            break;
        }
    }
    al_int_dispose(&out);
    return chosen;
}

static int build_route(Route *route, const EdgeTable *edges, int from, int to,
                       const int *prev) {
    ArrayList_Int rev;
    al_int_init(&rev);
    int cur = to;
    while (cur != from) {
        al_int_push(&rev, cur);
        cur = prev[cur];
    }
    al_int_push(&rev, from);
    for (size_t i = rev.size; i-- > 0;)
        al_int_push(&route->stations, rev.items[i]);
    al_int_dispose(&rev);

    int prev_line = -1;
    for (size_t i = 0; i + 1 < route->stations.size; i++) {
        int a = route->stations.items[i];
        int b = route->stations.items[i + 1];
        int eid = pick_edge(edges, a, b, prev_line);
        if (eid < 0)
            return -1;
        al_int_push(&route->edges_ids, eid);
        const Edge *e = edge_find_by_id(edges, eid);
        route->total_meters += e->cost_meters;
        route->total_seconds += e->cost_time_second;
        prev_line = e->line_id;
    }
    route->total_stations = (int)route->stations.size;

    for (size_t i = 0; i + 1 < route->edges_ids.size; i++) {
        const Edge *a = edge_find_by_id(edges, route->edges_ids.items[i]);
        const Edge *b = edge_find_by_id(edges, route->edges_ids.items[i + 1]);
        if (a->line_id != b->line_id)
            al_int_push(&route->transfers, route->stations.items[i + 1]);
    }
    return 0;
}

int router_find_route(const Graph *g, const Metro *metro, int from_id, int to_id,
                      RouteMetric metric, Route *route) {
    if (station_find_by_id(&metro->stations, from_id) == NULL ||
        station_find_by_id(&metro->stations, to_id) == NULL)
        return -1;

    route->total_stations = 0;
    route->total_meters = 0;
    route->total_seconds = 0;
    al_int_dispose(&route->stations);
    al_int_dispose(&route->edges_ids);
    al_int_dispose(&route->transfers);
    al_int_init(&route->stations);
    al_int_init(&route->edges_ids);
    al_int_init(&route->transfers);

    if (from_id == to_id) {
        al_int_push(&route->stations, from_id);
        route->total_stations = 1;
        return 0;
    }

    int *prev = malloc((size_t)g->capacity * sizeof(int));
    if (prev == NULL)
        return -1;
    for (int i = 0; i < g->capacity; i++)
        prev[i] = -1;

    int found;
    if (metric == ROUTE_MIN_STATIONS)
        found = bfs(g, from_id, to_id, prev);
    else
        found = dijkstra(g, &metro->edges, from_id, to_id, metric, prev);

    if (!found) {
        free(prev);
        return -1;
    }
    int rc = build_route(route, &metro->edges, from_id, to_id, prev);
    free(prev);
    return rc;
}