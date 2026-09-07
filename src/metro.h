#ifndef GZMP_METRO_H
#define GZMP_METRO_H

#include "adt/station.h"
#include "adt/line.h"
#include "adt/edge.h"

/*
 * Metro —— 顶层数据聚合
 *
 * 把三张表（站点 / 线路 / 区间边）聚合为一个整体：
 *   - io 层：metro_io_load 载入填充、metro_io_save 写回；
 *   - algo 层：graph_build 由其构建邻接表，router 查询路径；
 *   - render / ui 层：展示与交互统一操作这一个对象。
 * 生命周期：init 三个子表 → 使用 → dispose 三个子表。
 */

/* Metro —— 顶层数据聚合：io 载入/保存、ui 操作、render 展示共用 */
typedef struct {
    StationTable stations;
    LineTable    lines;
    EdgeTable    edges;
} Metro;

#endif