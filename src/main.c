#include <locale.h>
#include <stdio.h>
#include <string.h>
#include "ui/tui_state.h"
#ifdef _WIN32
#include <windows.h>
#endif
#include "ui/ui.h"

/*
 * main —— 程序入口与装配
 *
 * 1. Windows 控制台 UTF-8 化：
 *    - SetConsoleOutputCP/SetConsoleCP(65001)：控制台输入输出的代码页；
 *    - SetConsoleMode 开启 VT 转义解析，否则 ANSI 彩色会按字面显示；
 *    - setlocale(".UTF-8")：C 运行库按 UTF-8 处理多字节字符。
 * 2. 数据目录：默认 "data"，可传 argv[1] 指定（如测试用临时目录）。
 * 3. 其余全部交给 ui_main_loop（内部完成载入、菜单循环与释放）。
 */
int main(int argc, char *argv[]) {
    const char *data_dir = "data";
    int text = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--text")) text = 1;
        else data_dir = argv[i];
    }
    char error[256];
    if (project_io_recover(data_dir, error, sizeof(error))) { fprintf(stderr, "%s\n", error); return 1; }
#ifdef _WIN32
    if (text) {
    SetConsoleOutputCP(65001);   /* 控制台输出 UTF-8 */
    SetConsoleCP(65001);         /* 控制台输入 UTF-8 */
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(hOut, &mode))
        SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
#endif
    setlocale(LC_ALL, ".UTF-8");

    return (text ? ui_main_loop(data_dir) : tui_main_loop(data_dir)) == 0 ? 0 : 1;
}
