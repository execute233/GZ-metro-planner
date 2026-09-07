#ifndef GZMP_IO_METRO_IO_H
#define GZMP_IO_METRO_IO_H

#include <stddef.h>
#include "../metro.h"

/*
 * metro_io —— CSV 持久化层（接口定义）
 *
 * 数据文件：data/ 下 stations.csv、lines.csv、edges.csv（UTF-8 无 BOM）。
 * 载入（load）= 读文件 + 解析 + metro_io_validate 一致性校验，任一失败返回 -1；
 * 保存（save）= 三表写回三个 CSV。
 * 实现见 metro_io.c。
 */

/* 从 data_dir 读取三个 CSV 载入内存表，返回 0 成功 / -1 失败 */
int metro_io_load(const char *data_dir, Metro *metro);
/* 把三表写回 data_dir 的三个 CSV，返回 0 成功 / -1 失败 */
int metro_io_save(const char *data_dir, const Metro *metro);
/* 校验三表一致性：id 唯一、引用存在、边与站序一致、cost_meters > 0；
 * 返回 0 通过 / -1 不通过，错误详情写入 errbuf（需 errbuf_size 空间） */
int metro_io_validate(const Metro *metro, char *errbuf, size_t errbuf_size);

#endif