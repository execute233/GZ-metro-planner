#ifndef GZMP_LINE_DIAGRAM_H
#define GZMP_LINE_DIAGRAM_H
#include "map_render.h"

/* Draw a validated line_paths result in the left pane (rows 3 .. height-4).
 * Coordinates are in a vertically scrollable document; return its row count. */
int line_diagram_render(MapFrame *frame, const Metro *metro, int line_id,
                        const ArrayList_Int *paths, int width, int scroll);
#endif
