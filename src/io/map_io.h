#ifndef GZMP_MAP_IO_H
#define GZMP_MAP_IO_H
#include "../metro.h"
#include "../adt/map_layout.h"
#include <stdio.h>
int map_io_load(const char *dir, const Metro *metro, MapDocument *map, char *error, size_t capacity);
int map_io_validate(const Metro *metro, const MapDocument *map, char *error, size_t capacity);
int map_io_write(FILE *file, const MapDocument *map, int table);
#endif
