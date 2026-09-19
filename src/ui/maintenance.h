#ifndef GZMP_MAINTENANCE_H
#define GZMP_MAINTENANCE_H
#include "../metro.h"
#define MAINTENANCE_ACTION_COUNT 10

typedef struct {
    int active, action, step, confirm;
    size_t interval;
    Station station;
    Line line;
    EdgeTable edges;
    Edge original_edge; /* Existing interval selected for update, deletion or splitting. */
    char input[128], message[256];
} Maintenance;

void maintenance_close(Maintenance *edit);
/* Validate one complete form field. Return 1 when the user confirms saving,
 * 0 while editing, -1 for invalid input. No model or file is changed. */
int maintenance_submit(Maintenance *edit, const Metro *metro);
/* Apply the draft to a fresh database model and save it atomically. */
int maintenance_save(Maintenance *edit, const char *path);
#endif
