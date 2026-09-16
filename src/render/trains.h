#ifndef GZMP_TRAINS_H
#define GZMP_TRAINS_H
#include "map_render.h"

/* Both network and journey playback use this single speed setting. */
#define TRAIN_SPEED_MULTIPLIER 30.0
#define TRAIN_FRAME_MS 100
#define TRAIN_CARRIAGE_SCALE .35

typedef struct {
    Edge edge;
    MapSegment *segments;
    double *ends, length;
    size_t count;
} TrainGeometry;
typedef struct {
    int geometry, reverse; /* geometry == -1 is an invisible transition. */
    double end;
} TrainStep;
typedef struct {
    TrainStep *steps;
    size_t count;
    double duration;
} TrainTrack;
typedef struct {
    TrainGeometry *geometry;
    size_t count;
    TrainTrack *lines;
    size_t line_count;
    TrainTrack journey;
    double network_time, journey_time;
    int paused;
} Trains;
typedef struct {
    double x, y, dx, dy;
    int line_id, edge_id;
} TrainPosition;
int trains_init(Trains *trains, MapDb *map);
void trains_dispose(Trains *trains);
int trains_plan(Trains *trains, const Route *route);
void trains_advance(Trains *trains, double elapsed, int maintenance);
int trains_position(const Trains *trains, const TrainTrack *track, double time, TrainPosition *out);
void trains_render(const Trains *trains, int journey, MapFrame *frame, Viewport view, int cols, int rows);
#endif
