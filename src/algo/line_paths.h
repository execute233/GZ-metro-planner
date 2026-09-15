#ifndef GZMP_LINE_PATHS_H
#define GZMP_LINE_PATHS_H
#include "../metro.h"

/* For a validated Metro, return station IDs in connected runs separated by 0.
 * Runs end at endpoints/junctions; cycles repeat their first station at the end.
 * Every edge occurs once. Output must be zero-initialized; caller disposes it. */
int line_paths(const Metro *metro, int line_id, ArrayList_Int *out);
#endif
