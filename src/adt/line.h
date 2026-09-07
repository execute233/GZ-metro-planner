#ifndef GZMP_ADT_LINE_H
#define GZMP_ADT_LINE_H

#include "arraylist.h"

#define LINE_NAME_MAX 16
#define LINE_EN_MAX   32

/* Line —— 地铁线路；id 为主键，station_ids 为该线有序站序 */
typedef struct {
    int  id;                        /* 线路主键，外键引用依据 */
    char name[LINE_NAME_MAX];       /* 线路名，如 "1号线" */
    char en_name[LINE_EN_MAX];      /* 线路英文名 */
    int  color;                     /* ANSI 前景色号，渲染用 */
    ArrayList_Int station_ids;      /* 有序站序（外键 → stations.id） */
} Line;

DEFINE_ARRAYLIST(Line, Line, line)

/* LineTable —— 线路表，包装 ArrayList_Line */
typedef struct {
    ArrayList_Line rows;
} LineTable;

/* 初始化线路表 */
int  line_table_init(LineTable *t);
/* 释放线路表及每条线内部的 station_ids */
void line_table_dispose(LineTable *t);
/* 按 id 查找，命中返回表内指针（勿修改/释放），未命中 NULL */
Line *line_find_by_id(const LineTable *t, int id);
/* 按线路名查找，命中返回表内指针，未命中 NULL */
Line *line_find_by_name(const LineTable *t, const char *name);
/* 新增线路（自动分配 id 并回写 ln->id；station_ids 深拷贝），返回 0 成功 / -1 失败 */
int  line_add(LineTable *t, Line *ln);
/* 按 id 删除（含其 station_ids），返回 0 成功 / -1 未找到 */
int  line_remove(LineTable *t, int id);
/* 返回下一个可用 id（当前最大 id + 1），删除后不重用 */
int  line_next_id(const LineTable *t);

#endif