#include "graph.h"

#include <stdlib.h>
#include <string.h>

/*
 * graph —— 无向邻接表构建
 *
 * 地铁网络 = 稀疏无向图（站为节点、区间边为边）。
 * 存储：adj[站id] = 该站相邻站 id 列表，站 id 直接作数组下标，
 *   capacity = 最大站 id + 1。id 删除后留空洞，空洞节点为空列表，
 *   graph_neighbors 对越界 id 返回 NULL（调用方需判空）。
 * 每条边 from/to 双向挂载（无向），挂载前去重避免重复邻居。
 */

/* 列表中是否已含 id（防止同一条边重复挂载） */
static int has_id(const ArrayList_Int *list, int id) {
    for (size_t i = 0; i < list->size; i++) {
        if (list->items[i] == id)
            return 1;
    }
    return 0;
}

/* 由三表构建无向邻接表（每条边 from/to 双向挂载），返回 0 成功 / -1 失败 */
int graph_build(Graph *g, const Metro *metro) {
    /* 先求最大站 id，确定邻接表数组长度（含空洞） */
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

    /* 每条边双向挂载：from→to 与 to→from（图无向）；
     * 挂载前查重，防止共线段/重复边产生重复邻居 */
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

/* 释放邻接表（含每个站的邻居列表），之后可再次 build 复用 */
void graph_dispose(Graph *g) {
    if (g->adj == NULL)
        return;
    for (int i = 0; i < g->capacity; i++)
        al_int_dispose(&g->adj[i]);
    free(g->adj);
    g->adj = NULL;
    g->capacity = 0;
}

/* 取站 id 的邻居列表（指针，勿修改/释放），id 越界返回 NULL */
const ArrayList_Int *graph_neighbors(const Graph *g, int station_id) {
    /* 站 id 直接作下标；越界（含空洞外的负 id）返回 NULL 由调用方判空 */
    if (station_id < 0 || station_id >= g->capacity)
        return NULL;
    return &g->adj[station_id];
}