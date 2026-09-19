#ifndef GZMP_MAINTENANCE_FORM_H
#define GZMP_MAINTENANCE_FORM_H
#include "maintenance.h"
#include "../render/map_render.h"

typedef enum { FORM_TEXT, FORM_STATION_NAME, FORM_LINE_NAME, FORM_STATION_REF,
               FORM_LINE_REF, FORM_COLOR, FORM_COORDS, FORM_COST } FormFieldKind;
typedef struct {
    char label[80], value[128], error[160];
    FormFieldKind kind;
    size_t limit;
    int required;
    int station_id;
} MaintenanceField;
typedef struct {
    int active, selected, action, loaded, focus, scroll, station_count;
    size_t cursor;
    MaintenanceField *fields;
    int count;
    char target[128], message[256];
    int picking, candidate, match_count, matches[MAP_LIMIT];
    char search[128];
    size_t search_cursor;
} MaintenanceForm;
typedef enum { FORM_UP, FORM_DOWN, FORM_LEFT, FORM_RIGHT, FORM_HOME, FORM_END,
               FORM_BACKSPACE, FORM_DELETE, FORM_BACK, FORM_ENTER } MaintenanceFormKey;

void maintenance_form_close(MaintenanceForm *form);
void maintenance_form_open(MaintenanceForm *form, int selected);
/* Return 1 only when Confirm has produced a validated draft ready to save. */
int maintenance_form_key(MaintenanceForm *form, MaintenanceFormKey key,
                         const Metro *metro, Maintenance *draft);
void maintenance_form_type(MaintenanceForm *form, unsigned codepoint, const Metro *metro);
void maintenance_form_frame(MaintenanceForm *form, MapFrame *frame, const Metro *metro);
#endif
