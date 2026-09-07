#include "line.h"

#include <string.h>

/*
 * line —— 线路表 CRUD 实现
 *
 * 关键点：Line 内嵌 ArrayList_Int station_ids（动态分配），因此
 *   - line_add 必须深拷贝站序，外部对原列表的修改不影响表内数据；
 *   - line_remove / line_table_dispose 必须逐条释放 station_ids，防内存泄漏。
 */

int line_table_init(LineTable *t) {
    return al_line_init(&t->rows);
}

void line_table_dispose(LineTable *t) {
    for (size_t i = 0; i < t->rows.size; i++)
        al_int_dispose(&t->rows.items[i].station_ids);
    al_line_dispose(&t->rows);
}

Line *line_find_by_id(const LineTable *t, int id) {
    for (size_t i = 0; i < t->rows.size; i++) {
        if (t->rows.items[i].id == id)
            return &t->rows.items[i];
    }
    return NULL;
}

Line *line_find_by_name(const LineTable *t, const char *name) {
    for (size_t i = 0; i < t->rows.size; i++) {
        if (strcmp(t->rows.items[i].name, name) == 0)
            return &t->rows.items[i];
    }
    return NULL;
}

int line_add(LineTable *t, Line *ln) {
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

int line_remove(LineTable *t, int id) {
    for (size_t i = 0; i < t->rows.size; i++) {
        if (t->rows.items[i].id == id) {
            al_int_dispose(&t->rows.items[i].station_ids);
            return al_line_remove_at(&t->rows, i);
        }
    }
    return -1;
}

int line_next_id(const LineTable *t) {
    int max_id = 0;
    for (size_t i = 0; i < t->rows.size; i++) {
        if (t->rows.items[i].id > max_id)
            max_id = t->rows.items[i].id;
    }
    return max_id + 1;
}