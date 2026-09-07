#ifndef GZMP_ADT_EDGE_H
#define GZMP_ADT_EDGE_H

#include "arraylist.h"

/*
 * edge —— 区间边表（对应 data/edges.csv）
 *
 * 一条边 = 某条线路上两个相邻车站之间的区间，承担双重职责：
 *   1. 数据层：记录区间运行时长与里程（未来票价按里程计算）；
 *   2. 图论层：作为无向图的边参与 BFS/Dijkstra 最短路。
 * 方向约定：from→to 表示线路行驶方向，但地铁图无向，
 *   构建邻接表时 from/to 两侧都要挂载；查找类函数均双向匹配。
 * 约束：同一线路内不允许 from/to 互换的重复边（载入时校验）。
 */

/* Edge —— 线路区间（一条边）：相邻两站对，既作线路顺序记录又作图边 */
typedef struct {
    int id;                 /* 边主键，跨文件稳定，删除后不重用 */
    int line_id;            /* 所属线路主键（外键 → lines.id）；线路删除时边须级联删除 */
    int from_station_id;    /* 区间起点站主键（外键 → stations.id），线路行驶方向 */
    int to_station_id;      /* 区间终点站主键；同线不允许 from/to 互换的重复边 */
    int cost_time_second;   /* 区间运行时长（秒），最少时间路径的边权 */
    int cost_meters;        /* 区间里程（米），票价计算依据；不允许 0 或负数 */
} Edge;

DEFINE_ARRAYLIST(Edge, Edge, edge)

/* EdgeTable —— 边表，包装 ArrayList_Edge */
typedef struct {
    ArrayList_Edge rows;
} EdgeTable;

/* 初始化边表 */
int  edge_table_init(EdgeTable *t);
/* 释放内部数组，之后可再次 init 复用 */
void edge_table_dispose(EdgeTable *t);
/* 按边主键查找，命中返回表内指针（勿修改/释放），未命中 NULL */
Edge *edge_find_by_id(const EdgeTable *t, int id);
/* 按起点站查找：把所有 from_station_id == 参数 的边 id 追加到 out（仅追加不清空），
 * 返回追加条数（>=0），参数非法返回 -1 */
int edge_find_by_from_station(const EdgeTable *t, int station_id, ArrayList_Int *out);
/* 按终点站查找：语义同上，匹配 to_station_id */
int edge_find_by_to_station(const EdgeTable *t, int station_id, ArrayList_Int *out);
/* 按两端站查找：连接 a、b 的所有边 id 追加到 out（a→b 与 b→a 均命中，可多条），
 * 返回追加条数（>=0），参数非法返回 -1 */
int edge_find_between(const EdgeTable *t, int station_a, int station_b, ArrayList_Int *out);
/* 新增边（自动分配 id 并回写 e->id），返回 0 成功 / -1 失败 */
int  edge_add(EdgeTable *t, Edge *e);
/* 按 id 删除，返回 0 成功 / -1 未找到 */
int  edge_remove(EdgeTable *t, int id);
/* 返回下一个可用 id（当前最大 id + 1），删除后不重用 */
int  edge_next_id(const EdgeTable *t);

#endif