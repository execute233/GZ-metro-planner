#include "viewport.h"
#include <math.h>
MapPoint viewport_project(const Viewport *v, MapPoint p) {
    return (MapPoint){(p.x-v->center_x)*v->zoom+v->cols, (p.y-v->center_y)*v->zoom+v->rows*2};
}
MapPoint viewport_unproject(const Viewport *v, MapPoint p) {
    return (MapPoint){(p.x-v->cols)/v->zoom+v->center_x, (p.y-v->rows*2)/v->zoom+v->center_y};
}
void viewport_fit(Viewport *v, MapPoint min, MapPoint max, int reset_limits) {
    v->center_x = (min.x+max.x)/2; v->center_y = (min.y+max.y)/2;
    v->zoom = fmin(fmax(2, v->cols*2-20)/fmax(20, max.x-min.x), fmax(4, v->rows*4-16)/fmax(20, max.y-min.y));
    if (reset_limits || v->fit_zoom <= 0) v->fit_zoom = v->zoom;
    v->zoom = fmax(v->fit_zoom*0.5, fmin(v->fit_zoom*16, v->zoom));
}
void viewport_zoom_at(Viewport *v, double factor, MapPoint pixel) {
    MapPoint before = viewport_unproject(v, pixel);
    v->zoom = fmax(v->fit_zoom*0.5, fmin(v->fit_zoom*16, v->zoom*factor));
    MapPoint after = viewport_unproject(v, pixel);
    v->center_x += before.x-after.x; v->center_y += before.y-after.y;
}
