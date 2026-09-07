#include "station.h"

#include <string.h>

/*
 * station —— 站点表 CRUD 实现
 *
 * 存储：ArrayList_Station 动态数组，id 显式存储在元素内（非数组下标），
 *   删除元素不改变其他元素的 id，保证 lines/edges 中的外键引用稳定。
 * 查找：线性扫描（站点规模 ~60，O(n) 足够）；名称按字节级 strcmp 比较。
 */

/* 初始化站点表 */
int station_table_init(StationTable *t) {
    return al_station_init(&t->rows);
}

/* 释放内部数组，之后可再次 init 复用 */
void station_table_dispose(StationTable *t) {
    al_station_dispose(&t->rows);
}

/* 按 id 查找，命中返回表内指针（勿修改/释放），未命中 NULL */
Station *station_find_by_id(const StationTable *t, int id) {
    /* 线性扫描比较 id 字段（不依赖数组下标，删除后引用仍稳定） */
    for (size_t i = 0; i < t->rows.size; i++) {
        if (t->rows.items[i].id == id)
            return &t->rows.items[i];
    }
    return NULL;
}

/* 按中文名查找，命中返回表内指针，未命中 NULL */
Station *station_find_by_name(const StationTable *t, const char *name) {
    /* UTF-8 中文名按字节级 strcmp 比较（等长字节串比较安全） */
    for (size_t i = 0; i < t->rows.size; i++) {
        if (strcmp(t->rows.items[i].name, name) == 0)
            return &t->rows.items[i];
    }
    return NULL;
}

/* 新增站点（自动分配 id 并回写 st->id），返回 0 成功 / -1 失败 */
int station_add(StationTable *t, Station *st) {
    /* 值拷贝入表；id 由 next_id 分配并回写调用方，便于后续引用 */
    Station copy = *st;
    copy.id = station_next_id(t);
    if (al_station_push(&t->rows, copy) != 0)
        return -1;
    st->id = copy.id;
    return 0;
}

/* 按 id 删除，返回 0 成功 / -1 未找到 */
int station_remove(StationTable *t, int id) {
    /* 找到后按数组下标删除（remove_at 前移填补，其余元素 id 不变） */
    for (size_t i = 0; i < t->rows.size; i++) {
        if (t->rows.items[i].id == id)
            return al_station_remove_at(&t->rows, i);
    }
    return -1;
}

/* 返回下一个可用 id（当前最大 id + 1），删除后不重用 */
int station_next_id(const StationTable *t) {
    int max_id = 0;
    for (size_t i = 0; i < t->rows.size; i++) {
        if (t->rows.items[i].id > max_id)
            max_id = t->rows.items[i].id;
    }
    return max_id + 1;
}