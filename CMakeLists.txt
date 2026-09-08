cmake_minimum_required(VERSION 4.0)
project(GZ_metro_planner C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

include(cmake/MapDependencies.cmake)
include(cmake/PDCurses.cmake)
add_library(gzmp_map STATIC
    src/io/mvt.c src/io/map_db.c src/render/map_render.c src/ui/tui.c)
target_link_libraries(gzmp_map PUBLIC gzmp_sqlite gzmp_zlib)
target_compile_options(gzmp_map PRIVATE -Wall -Wextra -Wpedantic)

add_executable(GZ_metro_planner
    src/main.c
    src/adt/station.c
    src/adt/line.c
    src/adt/edge.c
    src/io/metro_io.c
    src/algo/graph.c
    src/algo/router.c
    src/render/render.c
    src/ui/ui.c
    src/ui/tui_backend_pdcurses.c
)
target_link_libraries(GZ_metro_planner PRIVATE gzmp_map gzmp_pdcurses)

enable_testing()
add_subdirectory(tests)
