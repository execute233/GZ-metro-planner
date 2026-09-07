#ifndef GZMP_UI_UI_H
#define GZMP_UI_UI_H

#include <stddef.h>
#include "../metro.h"

/*
 * ui —— 菜单交互层（接口定义）
 *
 * 组织所有用户操作：主菜单循环、规划路线、系统维护（线路/站点增删）。
 * 输入读取、校验、分发在此层；彩色输出委托 render，文件读写委托 metro_io。
 * 实现见 ui.c。
 */

/* 主菜单循环（内部完成载入与释放），直到用户退出；data_dir 为数据目录，
 * 返回 0 正常退出 / -1 载入失败 */
int ui_main_loop(const char *data_dir);
/* 设置数据目录（维护保存用，默认 "data"；测试可指向临时目录） */
void ui_set_data_dir(const char *dir);
/* 读取一行输入并去除首尾空白，返回 0 成功 / -1 EOF */
int ui_read_line(char *buf, size_t size);
/* 交互式规划路线：输入起终点（中文名），选目标后展示 */
void ui_plan_route(Metro *metro);
/* 交互式维护：线路/站点增删，操作后写回数据文件 */
void ui_maintain(Metro *metro);

#endif