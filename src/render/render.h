#ifndef GZMP_RENDER_RENDER_H
#define GZMP_RENDER_RENDER_H

#include "../metro.h"
#include "../algo/router.h"

/*
 * render —— 终端渲染层（接口定义）
 *
 * 纯输出层：只负责把数据"画"到终端，不读取任何用户输入（输入归 ui 层）。
 * 输出样式：ANSI 彩色（线路色号）、换乘站加粗、UTF-8 显示宽度对齐。
 * 实现见 render.c。
 */

/* 计算 UTF-8 字符串显示宽度（CJK 按 2 列），用于对齐 */
int render_display_width(const char *s);
/* 彩色输出单条线路：线名（线路色）+ 沿线站点，换乘站高亮 */
void render_line(const Line *line, const Metro *metro);
/* 彩色输出全部线路 */
void render_all_lines(const Metro *metro);
/* 输出路径结果：站点序列、换乘信息、三项统计（站点/里程/时长） */
void render_route(const Route *route, const Metro *metro);

#endif