#include "line.h"

#include <string.h>

/*
 * line —— 线路表 CRUD 实现
 *
 * 关键点：Line 内嵌 ArrayList_Int station_ids（动态分配），因此
 *   - line_add 必须深拷贝站序，外部对原列表的修改不影响表内数据；
 *   - line_remove / line_table_dispose 必须逐条释放 station_ids，防内存泄漏。
 */

/* 初始化线路表 */
int line_table_init(LineTable *t) {
    return al_line_init(&t->rows);
}

/* 释放线路表及每条线内部的 station_ids */
void line_table_dispose(LineTable *t) {
    /* 逐条先释放内嵌站序列表，再释放表数组（防内存泄漏） */
    for (size_t i = 0; i < t->rows.size; i++)
        al_int_dispose(&t->rows.items[i].station_ids);
    al_line_dispose(&t->rows);
}

/* 按 id 查找，命中返回表内指针（勿修改/释放），未命中 NULL */
Line *line_find_by_id(const LineTable *t, int id) {
    for (size_t i = 0; i < t->rows.size; i++) {
        if (t->rows.items[i].id == id)
            return &t->rows.items[i];
    }
    return NULL;
}

/* 按线路名查找，命中返回表内指针，未命中 NULL */
Line *line_find_by_name(const LineTable *t, const char *name) {
    for (size_t i = 0; i < t->rows.size; i++) {
        if (strcmp(t->rows.items[i].name, name) == 0)
            return &t->rows.items[i];
    }
    return NULL;
}

/* 新增线路（自动分配 id 并回写 ln->id；station_ids 深拷贝），返回 0 成功 / -1 失败 */
int line_add(LineTable *t, Line *ln) {
    /* 深拷贝站序：外部原列表后续修改不影响表内数据 */
    Line copy = *ln;
    copy.id = line_next_id(t);
    al_int_init(&copy.station_ids);
    for (size_t i = 0; i < ln->station_ids.size; i++)
        al_int_push(&copy.station_ids, ln->station_ids.items[i]);
    if (al_line_push(&t->rows, copy) != 0) {
        al_int_dispose(&copy.station_ids);
        return -1;
    }
    ln->id = copy.id;
    return 0;
}

/* 按 id 删除（含其 station_ids），返回 0 成功 / -1 未找到 */
int line_remove(LineTable *t, int id) {
    for (size_t i = 0; i < t->rows.size; i++) {
        if (t->rows.items[i].id == id) {
            al_int_dispose(&t->rows.items[i].station_ids);
            return al_line_remove_at(&t->rows, i);
        }
    }
    return -1;
}

/* 返回下一个可用 id（当前最大 id + 1），删除后不重用 */
int line_next_id(const LineTable *t) {
    int max_id = 0;
    for (size_t i = 0; i < t->rows.size; i++) {
        if (t->rows.items[i].id > max_id)
            max_id = t->rows.items[i].id;
    }
    return max_id + 1;
}