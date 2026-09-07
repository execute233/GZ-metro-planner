#include "edge.h"

/*
 * edge —— 区间边表 CRUD 实现
 *
 * 三个查找函数的共同约定：把匹配的"边 id"追加到调用方提供的 out 列表
 * （只追加不清空，便于调用方复用同一个 out 做多次合并查询）；
 * 返回追加条数，参数非法返回 -1。
 * 图是无向的：edge_find_between 对 (a→b) 与 (b→a) 两种存储方向都命中。
 */

/* 初始化边表 */
int edge_table_init(EdgeTable *t) {
    return al_edge_init(&t->rows);
}

/* 释放内部数组，之后可再次 init 复用 */
void edge_table_dispose(EdgeTable *t) {
    al_edge_dispose(&t->rows);
}

/* 按边主键查找，命中返回表内指针（勿修改/释放），未命中 NULL */
Edge *edge_find_by_id(const EdgeTable *t, int id) {
    for (size_t i = 0; i < t->rows.size; i++) {
        if (t->rows.items[i].id == id)
            return &t->rows.items[i];
    }
    return NULL;
}

/* 按起点站查找：把所有 from_station_id == 参数 的边 id 追加到 out（仅追加不清空），
 * 返回追加条数（>=0），参数非法返回 -1 */
int edge_find_by_from_station(const EdgeTable *t, int station_id, ArrayList_Int *out) {
    if (out == NULL)
        return -1;
    int n = 0;
    /* 线性扫描匹配 from_station_id，命中则把边 id 追加到 out */
    for (size_t i = 0; i < t->rows.size; i++) {
        if (t->rows.items[i].from_station_id == station_id) {
            if (al_int_push(out, t->rows.items[i].id) != 0)
                return -1;
            n++;
        }
    }
    return n;
}

/* 按终点站查找：语义同上，匹配 to_station_id */
int edge_find_by_to_station(const EdgeTable *t, int station_id, ArrayList_Int *out) {
    if (out == NULL)
        return -1;
    int n = 0;
    for (size_t i = 0; i < t->rows.size; i++) {
        if (t->rows.items[i].to_station_id == station_id) {
            if (al_int_push(out, t->rows.items[i].id) != 0)
                return -1;
            n++;
        }
    }
    return n;
}

/* 按两端站查找：连接 a、b 的所有边 id 追加到 out（a→b 与 b→a 均命中，可多条），
 * 返回追加条数（>=0），参数非法返回 -1 */
int edge_find_between(const EdgeTable *t, int station_a, int station_b, ArrayList_Int *out) {
    if (out == NULL)
        return -1;
    int n = 0;
    /* 图无向：两种存储方向都算命中；共线段（不同线路同两端）会命中多条 */
    for (size_t i = 0; i < t->rows.size; i++) {
        const Edge *e = &t->rows.items[i];
        if ((e->from_station_id == station_a && e->to_station_id == station_b) ||
            (e->from_station_id == station_b && e->to_station_id == station_a)) {
            if (al_int_push(out, e->id) != 0)
                return -1;
            n++;
        }
    }
    return n;
}

/* 新增边（自动分配 id 并回写 e->id），返回 0 成功 / -1 失败 */
int edge_add(EdgeTable *t, Edge *e) {
    /* 值拷贝入表；id 由 next_id 分配并回写调用方 */
    Edge copy = *e;
    copy.id = edge_next_id(t);
    if (al_edge_push(&t->rows, copy) != 0)
        return -1;
    e->id = copy.id;
    return 0;
}

/* 按 id 删除，返回 0 成功 / -1 未找到 */
int edge_remove(EdgeTable *t, int id) {
    for (size_t i = 0; i < t->rows.size; i++) {
        if (t->rows.items[i].id == id)
            return al_edge_remove_at(&t->rows, i);
    }
    return -1;
}

/* 返回下一个可用 id（当前最大 id + 1），删除后不重用 */
int edge_next_id(const EdgeTable *t) {
    int max_id = 0;
    for (size_t i = 0; i < t->rows.size; i++) {
        if (t->rows.items[i].id > max_id)
            max_id = t->rows.items[i].id;
    }
    return max_id + 1;
}