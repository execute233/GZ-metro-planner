add_library(gzmp_sqlite STATIC third_party/sqlite/sqlite3.c)
target_include_directories(gzmp_sqlite SYSTEM PUBLIC third_party/sqlite)
target_compile_definitions(gzmp_sqlite PRIVATE SQLITE_THREADSAFE=0 SQLITE_OMIT_LOAD_EXTENSION)
set_property(TARGET gzmp_sqlite PROPERTY C_STANDARD 17)

add_library(gzmp_zlib STATIC
    third_party/zlib/adler32.c third_party/zlib/crc32.c
    third_party/zlib/inflate.c third_party/zlib/inftrees.c
    third_party/zlib/inffast.c third_party/zlib/zutil.c)
target_include_directories(gzmp_zlib SYSTEM PUBLIC third_party/zlib)
set_property(TARGET gzmp_zlib PROPERTY C_STANDARD 17)
