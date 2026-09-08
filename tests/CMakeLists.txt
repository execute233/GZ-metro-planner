# 各模块单元测试目标按任务逐个追加
add_executable(test_arraylist test_arraylist.c)
add_test(NAME arraylist COMMAND test_arraylist)

add_executable(test_station test_station.c ../src/adt/station.c)
target_include_directories(test_station PRIVATE ../src/adt)
add_test(NAME station COMMAND test_station)

add_executable(test_line test_line.c ../src/adt/line.c)
target_include_directories(test_line PRIVATE ../src/adt)
add_test(NAME line COMMAND test_line)

add_executable(test_edge test_edge.c ../src/adt/edge.c)
target_include_directories(test_edge PRIVATE ../src/adt)
add_test(NAME edge COMMAND test_edge)

add_executable(test_graph test_graph.c
    ../src/adt/station.c ../src/adt/line.c ../src/adt/edge.c ../src/algo/graph.c)
target_include_directories(test_graph PRIVATE ../src)
add_test(NAME graph COMMAND test_graph)

add_executable(test_router test_router.c
    ../src/adt/station.c ../src/adt/line.c ../src/adt/edge.c
    ../src/algo/graph.c ../src/algo/router.c)
target_include_directories(test_router PRIVATE ../src)
add_test(NAME router COMMAND test_router)

add_executable(test_map test_map.c
    ../src/adt/station.c ../src/adt/line.c ../src/adt/edge.c
    ../src/algo/graph.c ../src/algo/router.c)
target_link_libraries(test_map PRIVATE gzmp_map)
target_compile_options(test_map PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME map COMMAND test_map "${PROJECT_SOURCE_DIR}/data/metro.mbtiles")

add_executable(test_maintenance test_maintenance.c
    ../src/adt/station.c ../src/adt/line.c ../src/adt/edge.c
    ../src/algo/graph.c ../src/algo/router.c)
target_link_libraries(test_maintenance PRIVATE gzmp_map)
add_test(NAME maintenance COMMAND test_maintenance "${PROJECT_SOURCE_DIR}/data/metro.mbtiles")
