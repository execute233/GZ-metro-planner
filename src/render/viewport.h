#ifndef GZMP_VIEWPORT_H
#define GZMP_VIEWPORT_H
#include "../adt/map_layout.h"
typedef struct { double center_x, center_y, zoom, fit_zoom; int cols, rows; } Viewport;
MapPoint viewport_project(const Viewport *v, MapPoint p);
MapPoint viewport_unproject(const Viewport *v, MapPoint p);
void viewport_fit(Viewport *v, MapPoint min, MapPoint max, int reset_limits);
void viewport_zoom_at(Viewport *v, double factor, MapPoint pixel);
#endif
