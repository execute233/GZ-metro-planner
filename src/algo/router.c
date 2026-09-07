#include "router.h"

#include <stdlib.h>

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

/* 找 (a,b) 之间的边：共线段优先延续 prev_line，否则取表序第一条；无匹配返回 NULL */
static const Edge *find_edge(const EdgeTable *edges, int a, int b, int prev_line) {
    const Edge *first = NULL;
    for (size_t i = 0; i < edges->rows.size; i++) {
        const Edge *e = &edges->rows.items[i];
        if ((e->from_station_id == a && e->to_station_id == b) ||
            (e->from_station_id == b && e->to_station_id == a)) {
            if (e->line_id == prev_line)
                return e;
            if (first == NULL)
                first = e;
        }
    }
    return first;
}

/* 找 (a,b) 之间按 metric 的最优边（共线段取最小权，与换乘不另计费一致）；无匹配返回 NULL */
static const Edge *find_best_edge(const EdgeTable *edges, int a, int b,
                                  RouteMetric metric) {
    const Edge *best = NULL;
    for (size_t i = 0; i < edges->rows.size; i++) {
        const Edge *e = &edges->rows.items[i];
        if ((e->from_station_id == a && e->to_station_id == b) ||
            (e->from_station_id == b && e->to_station_id == a)) {
            if (best == NULL) {
                best = e;
                continue;
            }
            int ew = (metric == ROUTE_MIN_DISTANCE) ? e->cost_meters
                                                    : e->cost_time_second;
            int bw = (metric == ROUTE_MIN_DISTANCE) ? best->cost_meters
                                                    : best->cost_time_second;
            if (ew < bw)
                best = e;
        }
    }
    return best;
}

/* prev 记录前驱站；prev_edge 记录到达该站所经边的 id（与 prev 一一对应） */
static int bfs(const Graph *g, const EdgeTable *edges, int from, int to,
               int *prev, int *prev_edge) {
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
                const Edge *e = find_edge(edges, cur, v, -1);
                prev_edge[v] = e != NULL ? e->id : -1;
                al_int_push(&queue, v);
            }
        }
    }
    al_int_dispose(&queue);
    return 0;
}

static int dijkstra(const Graph *g, const EdgeTable *edges, int from, int to,
                    RouteMetric metric, int *prev, int *prev_edge) {
    int n = g->capacity;
    int *dist = malloc((size_t)n * sizeof(int));
    char *done = calloc((size_t)n, sizeof(char));
    if (dist == NULL || done == NULL) {
        free(dist);
        free(done);
        return 0;
    }
    for (int i = 0; i < n; i++)
        dist[i] = INF;
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
            const Edge *e = find_best_edge(edges, u, v, metric);
            if (e == NULL)
                continue;
            int w = (metric == ROUTE_MIN_DISTANCE) ? e->cost_meters
                                                   : e->cost_time_second;
            if (dist[u] + w < dist[v]) {
                dist[v] = dist[u] + w;
                prev[v] = u;
                prev_edge[v] = e->id;
            }
        }
    }
    int found = (prev[to] != -1);
    free(dist);
    free(done);
    return found;
}

/* 沿 prev_edge 还原路径并汇总统计 */
static int build_route(Route *route, const EdgeTable *edges, int from, int to,
                       const int *prev, const int *prev_edge) {
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

    for (size_t i = 1; i < route->stations.size; i++) {
        int eid = prev_edge[route->stations.items[i]];
        const Edge *e = edge_find_by_id(edges, eid);
        if (e == NULL)
            return -1;
        al_int_push(&route->edges_ids, eid);
        route->total_meters += e->cost_meters;
        route->total_seconds += e->cost_time_second;
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
    int *prev_edge = malloc((size_t)g->capacity * sizeof(int));
    if (prev == NULL || prev_edge == NULL) {
        free(prev);
        free(prev_edge);
        return -1;
    }
    for (int i = 0; i < g->capacity; i++) {
        prev[i] = -1;
        prev_edge[i] = -1;
    }

    int found;
    if (metric == ROUTE_MIN_STATIONS)
        found = bfs(g, &metro->edges, from_id, to_id, prev, prev_edge);
    else
        found = dijkstra(g, &metro->edges, from_id, to_id, metric, prev, prev_edge);

    if (!found) {
        free(prev);
        free(prev_edge);
        return -1;
    }
    int rc = build_route(route, &metro->edges, from_id, to_id, prev, prev_edge);
    free(prev);
    free(prev_edge);
    return rc;
}