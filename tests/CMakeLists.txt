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