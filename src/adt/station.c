#include "station.h"

#include <string.h>

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