#ifndef GZMP_IO_METRO_IO_H
#define GZMP_IO_METRO_IO_H
#include <stddef.h>
#include <sqlite3.h>
#include "../metro.h"

#define METRO_STATION_LIMIT 4096
#define METRO_LINE_LIMIT 128
#define METRO_EDGE_LIMIT (METRO_STATION_LIMIT * 4)

/* Accept a data directory or an explicit .mbtiles path. */
int metro_io_path(const char *arg, char *path, size_t capacity);
/* SQLite is the sole runtime data source. metro must be initialized; failed
 * loads leave it unchanged. Successful loads replace and release old tables. */
int metro_io_load(const char *data_path, Metro *metro);
int metro_io_load_db(sqlite3 *db, Metro *metro);
/* Save to an existing GZMP database. Model and affected map tiles commit in one
 * transaction; failure leaves the database unchanged. */
int metro_io_save(const char *data_path, const Metro *metro);
int metro_io_validate(const Metro *metro, char *errbuf, size_t errbuf_size);
#endif
