cmake_minimum_required(VERSION 4.0)
project(GZ_metro_planner C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(GZ_metro_planner
    src/main.c
)

enable_testing()
add_subdirectory(tests)