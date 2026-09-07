#include <locale.h>
#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include "ui/ui.h"

int main(int argc, char *argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(65001);   /* 控制台输出 UTF-8 */
    SetConsoleCP(65001);         /* 控制台输入 UTF-8 */
#endif
    setlocale(LC_ALL, ".UTF-8");

    const char *data_dir = (argc > 1) ? argv[1] : "data";
    return ui_main_loop(data_dir) == 0 ? 0 : 1;
}