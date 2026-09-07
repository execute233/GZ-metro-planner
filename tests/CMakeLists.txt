# 各模块单元测试目标按任务逐个追加
add_executable(test_arraylist test_arraylist.c)
add_test(NAME arraylist COMMAND test_arraylist)

add_executable(test_station test_station.c ../src/adt/station.c)
target_include_directories(test_station PRIVATE ../src/adt)
add_test(NAME station COMMAND test_station)