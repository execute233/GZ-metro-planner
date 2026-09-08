#ifndef GZMP_TILE_WRITER_H
#define GZMP_TILE_WRITER_H
#include "../metro.h"
#include <sqlite3.h>

/* Update affected tiles inside the caller's transaction. Existing polylines
 * stay intact; new/moved edges use straight schematic segments. */
int tile_writer_update(sqlite3 *db, const Metro *before, const Metro *after);
#endif
