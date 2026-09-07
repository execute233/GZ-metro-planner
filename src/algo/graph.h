#ifndef GZMP_ALGO_GRAPH_H
#define GZMP_ALGO_GRAPH_H

#include "../adt/arraylist.h"
#include "../metro.h"

/* graph —— 无向邻接表 */

typedef struct {
    ArrayList_Int *adj;   /* 邻接表：adj[站id] = 相邻站 id 列表，按 id 直接索引 */
    int capacity;         /* adj 数组长度 = 最大站 id + 1（id 删除后留空洞，直接跳过） */
} Graph;

/* 由三表构建无向邻接表（每条边 from/to 双向挂载），返回 0 成功 / -1 失败 */
int graph_build(Graph *g, const Metro *metro);
/* 释放邻接表（含每个站的邻居列表），之后可再次 build 复用 */
void graph_dispose(Graph *g);
/* 取站 id 的邻居列表（指针，勿修改/释放），id 越界返回 NULL */
const ArrayList_Int *graph_neighbors(const Graph *g, int station_id);

#endif