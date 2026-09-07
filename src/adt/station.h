#ifndef GZMP_ADT_STATION_H
#define GZMP_ADT_STATION_H

#include "arraylist.h"

#define STATION_NAME_MAX     33   /* UTF-8 中文站名，含 '\0' */
#define STATION_PINYIN_MAX   65   /* 全拼 */
#define STATION_EN_MAX       49   /* 英文名 */

/* Station —— 地铁站点；id 为主键，跨文件稳定，删除后不重用 */
typedef struct {
    int  id;                        /* 站点主键，外键引用依据 */
    char name[STATION_NAME_MAX];    /* 中文站名，同名判定换乘的唯一依据 */
    char pinyin_name[STATION_PINYIN_MAX]; /* 全拼，预留拼音搜索 */
    char en_name[STATION_EN_MAX];   /* 英文名 */
} Station;

DEFINE_ARRAYLIST(Station, Station, station)

/* StationTable —— 站点表，包装 ArrayList_Station */
typedef struct {
    ArrayList_Station rows;
} StationTable;

/* 初始化站点表 */
int        station_table_init(StationTable *t);
/* 释放内部数组，之后可再次 init 复用 */
void       station_table_dispose(StationTable *t);
/* 按 id 查找，命中返回表内指针（勿修改/释放），未命中 NULL */
Station   *station_find_by_id(const StationTable *t, int id);
/* 按中文名查找，命中返回表内指针，未命中 NULL */
Station   *station_find_by_name(const StationTable *t, const char *name);
/* 新增站点（自动分配 id 并回写 st->id），返回 0 成功 / -1 失败 */
int        station_add(StationTable *t, Station *st);
/* 按 id 删除，返回 0 成功 / -1 未找到 */
int        station_remove(StationTable *t, int id);
/* 返回下一个可用 id（当前最大 id + 1），删除后不重用 */
int        station_next_id(const StationTable *t);

#endif