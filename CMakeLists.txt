cmake_minimum_required(VERSION 4.0)
project(GZ_metro_planner C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

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
)

enable_testing()
add_subdirectory(tests)