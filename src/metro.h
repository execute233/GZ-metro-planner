#ifndef GZMP_METRO_H
#define GZMP_METRO_H

#include "adt/station.h"
#include "adt/line.h"
#include "adt/edge.h"

/* Metro —— 顶层数据聚合：io 载入/保存、ui 操作、render 展示共用 */
typedef struct {
    StationTable stations;
    LineTable    lines;
    EdgeTable    edges;
} Metro;

#endif