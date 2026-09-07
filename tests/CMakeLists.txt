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

add_executable(test_metro_io test_metro_io.c
    ../src/adt/station.c ../src/adt/line.c ../src/adt/edge.c ../src/io/metro_io.c)
target_include_directories(test_metro_io PRIVATE ../src)
add_test(NAME metro_io COMMAND test_metro_io)

add_executable(test_graph test_graph.c
    ../src/adt/station.c ../src/adt/line.c ../src/adt/edge.c ../src/algo/graph.c)
target_include_directories(test_graph PRIVATE ../src)
add_test(NAME graph COMMAND test_graph)

add_executable(test_router test_router.c
    ../src/adt/station.c ../src/adt/line.c ../src/adt/edge.c
    ../src/algo/graph.c ../src/algo/router.c)
target_include_directories(test_router PRIVATE ../src)
add_test(NAME router COMMAND test_router)

add_executable(test_render test_render.c
    ../src/adt/station.c ../src/adt/line.c ../src/adt/edge.c ../src/render/render.c)
target_include_directories(test_render PRIVATE ../src)
add_test(NAME render COMMAND test_render)

add_executable(test_ui test_ui.c
    ../src/adt/station.c ../src/adt/line.c ../src/adt/edge.c
    ../src/io/metro_io.c ../src/algo/graph.c ../src/algo/router.c
    ../src/render/render.c ../src/ui/ui.c)
target_include_directories(test_ui PRIVATE ../src)
add_test(NAME ui COMMAND test_ui)