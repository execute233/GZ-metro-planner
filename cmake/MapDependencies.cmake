add_library(gzmp_sqlite STATIC lib/sqlite/sqlite3.c)
target_include_directories(gzmp_sqlite SYSTEM PUBLIC lib/sqlite)
target_compile_definitions(gzmp_sqlite PRIVATE SQLITE_THREADSAFE=0 SQLITE_OMIT_LOAD_EXTENSION)
set_property(TARGET gzmp_sqlite PROPERTY C_STANDARD 17)

add_library(gzmp_zlib STATIC
    lib/zlib/adler32.c lib/zlib/crc32.c
    lib/zlib/inflate.c lib/zlib/inftrees.c
    lib/zlib/inffast.c lib/zlib/zutil.c
    lib/zlib/deflate.c lib/zlib/trees.c)
target_include_directories(gzmp_zlib SYSTEM PUBLIC lib/zlib)
set_property(TARGET gzmp_zlib PROPERTY C_STANDARD 17)
