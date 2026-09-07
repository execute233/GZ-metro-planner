#ifndef GZMP_ALGO_ROUTER_H
#define GZMP_ALGO_ROUTER_H

#include "../adt/arraylist.h"
#include "../metro.h"
#include "graph.h"

/*
 * router —— 路径规划与换乘提取（接口定义）
 *
 * 输入：无向邻接表 + 三表；输出：Route（路径站点序列、每段边 id、
 * 换乘站序列、三项统计）。算法实现见 router.c：
 *   ROUTE_MIN_STATIONS —— BFS；ROUTE_MIN_DISTANCE / ROUTE_MIN_TIME —— Dijkstra。
 * 调用约定：route 用前必须 route_init，用后必须 route_dispose；
 *   router_find_route 返回 0 前会清空并重建 route 内容，可安全复用。
 */

/* 路径优化目标 */
typedef enum {
    ROUTE_MIN_STATIONS = 0,  /* 最少站点：BFS（无权，每跳权重 1） */
    ROUTE_MIN_DISTANCE,      /* 最短路程：Dijkstra，边权 = cost_meters */
    ROUTE_MIN_TIME           /* 最少时间：Dijkstra，边权 = cost_time_second */
} RouteMetric;

/* 路径查询结果（瞬态，用后必须 dispose） */
typedef struct {
    ArrayList_Int stations;   /* 路径站点 id 序列（含起终点） */
    ArrayList_Int edges_ids;  /* 每段区间对应边 id，长度 = stations.size - 1 */
    ArrayList_Int transfers;  /* 换乘站点 id 序列（无换乘则为空） */
    int total_stations;       /* 经过站点总数（含起终点） */
    int total_meters;         /* 总里程（米），票价计算依据 */
    int total_seconds;        /* 总运行时长（秒） */
} Route;

/* 初始化路由结果，使用前必须先调用 */
int route_init(Route *route);
/* 释放路由结果内部数组，之后可再次 init 复用 */
void route_dispose(Route *route);
/* 按 metric 计算 from_id 到 to_id 的最优路径，成功返回 0 并填充 route；
 * 起终点不存在或不可达返回 -1 */
int router_find_route(const Graph *g, const Metro *metro, int from_id, int to_id,
                      RouteMetric metric, Route *route);

#endif