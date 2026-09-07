#include "edge.h"

int edge_table_init(EdgeTable *t) {
    return al_edge_init(&t->rows);
}

void edge_table_dispose(EdgeTable *t) {
    al_edge_dispose(&t->rows);
}

Edge *edge_find_by_id(const EdgeTable *t, int id) {
    for (size_t i = 0; i < t->rows.size; i++) {
        if (t->rows.items[i].id == id)
            return &t->rows.items[i];
    }
    return NULL;
}

int edge_find_by_from_station(const EdgeTable *t, int station_id, ArrayList_Int *out) {
    if (out == NULL)
        return -1;
    int n = 0;
    for (size_t i = 0; i < t->rows.size; i++) {
        if (t->rows.items[i].from_station_id == station_id) {
            if (al_int_push(out, t->rows.items[i].id) != 0)
                return -1;
            n++;
        }
    }
    return n;
}

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

int edge_find_between(const EdgeTable *t, int station_a, int station_b, ArrayList_Int *out) {
    if (out == NULL)
        return -1;
    int n = 0;
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

int edge_add(EdgeTable *t, Edge *e) {
    Edge copy = *e;
    copy.id = edge_next_id(t);
    if (al_edge_push(&t->rows, copy) != 0)
        return -1;
    e->id = copy.id;
    return 0;
}

int edge_remove(EdgeTable *t, int id) {
    for (size_t i = 0; i < t->rows.size; i++) {
        if (t->rows.items[i].id == id)
            return al_edge_remove_at(&t->rows, i);
    }
    return -1;
}

int edge_next_id(const EdgeTable *t) {
    int max_id = 0;
    for (size_t i = 0; i < t->rows.size; i++) {
        if (t->rows.items[i].id > max_id)
            max_id = t->rows.items[i].id;
    }
    return max_id + 1;
}