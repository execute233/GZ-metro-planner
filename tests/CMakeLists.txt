# 各模块单元测试目标按任务逐个追加
add_executable(test_arraylist test_arraylist.c)
add_test(NAME arraylist COMMAND test_arraylist)