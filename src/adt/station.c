#include "station.h"

#include <string.h>

/*
 * station —— 站点表 CRUD 实现
 *
 * 存储：ArrayList_Station 动态数组，id 显式存储在元素内（非数组下标），
 *   删除元素不改变其他元素的 id，保证 lines/edges 中的外键引用稳定。
 * 查找：线性扫描（站点规模 ~60，O(n) 足够）；名称按字节级 strcmp 比较。
 */

int station_table_init(StationTable *t) {
    return al_station_init(&t->rows);
}

void station_table_dispose(StationTable *t) {
    al_station_dispose(&t->rows);
}

Station *station_find_by_id(const StationTable *t, int id) {
    for (size_t i = 0; i < t->rows.size; i++) {
        if (t->rows.items[i].id == id)
            return &t->rows.items[i];
    }
    return NULL;
}

Station *station_find_by_name(const StationTable *t, const char *name) {
    for (size_t i = 0; i < t->rows.size; i++) {
        if (strcmp(t->rows.items[i].name, name) == 0)
            return &t->rows.items[i];
    }
    return NULL;
}

int station_add(StationTable *t, Station *st) {
    Station copy = *st;
    copy.id = station_next_id(t);
    if (al_station_push(&t->rows, copy) != 0)
        return -1;
    st->id = copy.id;
    return 0;
}

int station_remove(StationTable *t, int id) {
    for (size_t i = 0; i < t->rows.size; i++) {
        if (t->rows.items[i].id == id)
            return al_station_remove_at(&t->rows, i);
    }
    return -1;
}

int station_next_id(const StationTable *t) {
    int max_id = 0;
    for (size_t i = 0; i < t->rows.size; i++) {
        if (t->rows.items[i].id > max_id)
            max_id = t->rows.items[i].id;
    }
    return max_id + 1;
}