#ifndef GZMP_PROJECT_IO_H
#define GZMP_PROJECT_IO_H
#include "map_io.h"
int project_io_recover(const char *dir, char *error, size_t cap);
int project_io_save(const char *dir, const Metro *metro, const MapDocument *map, char *error, size_t cap);
int project_clone(Metro *out, const Metro *source);
void project_dispose(Metro *metro);
#endif
