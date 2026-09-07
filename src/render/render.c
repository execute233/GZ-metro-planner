#include "render.h"

#include <stdio.h>

/*
 * render —— 终端渲染层实现
 *
 * 纯输出、不处理输入（交互归 ui 层）。输出用 ANSI 转义序列着色：
 *   \033[%dm —— 前景色（31 红 / 32 绿 / 33 黄 / 34 蓝 ...，取自 Line.color）；
 *   \033[1m —— 加粗（换乘站标记）；\033[0m —— 复位。
 * Windows 下需由 main 开启 ENABLE_VIRTUAL_TERMINAL_PROCESSING 才能解析转义。
 * 中文字符显示宽度为 2 列（CJK 双宽），对齐必须用 render_display_width
 * 而不是 strlen 的字节数。
 */

/* UTF-8 字符串显示宽度：首字节 >= 0x80 视为 CJK（跳过后缀续字节，计 2 列） */
int render_display_width(const char *s) {
    int w = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        if (*p >= 0x80) {
            p++;                            /* 跳过 UTF-8 首字节 */
            while (*p >= 0x80 && *p < 0xC0)
                p++;                        /* 跳过续字节 */
            w += 2;
        } else {
            w += 1;
            p++;
        }
    }
    return w;
}

/* 统计站 id 出现在几条线路中（>1 即换乘站） */
static int station_line_count(const Metro *metro, int station_id) {
    int n = 0;
    for (size_t i = 0; i < metro->lines.rows.size; i++) {
        const ArrayList_Int *ids = &metro->lines.rows.items[i].station_ids;
        for (size_t k = 0; k < ids->size; k++) {
            if (ids->items[k] == station_id) {
                n++;
                break;
            }
        }
    }
    return n;
}

static const char *station_name(const Metro *metro, int station_id) {
    Station *st = station_find_by_id(&metro->stations, station_id);
    return st != NULL ? st->name : "?";
}

void render_line(const Line *line, const Metro *metro) {
    printf("\033[%dm%s\033[0m", line->color, line->name);
    int w = render_display_width(line->name);
    for (int i = w; i < 8; i++)   /* 线名按显示宽度对齐到 8 列 */
        putchar(' ');
    printf(": ");
    for (size_t i = 0; i < line->station_ids.size; i++) {
        int id = line->station_ids.items[i];
        if (i > 0)
            printf(" - ");
        if (station_line_count(metro, id) > 1)
            printf("\033[1m%s\033[0m", station_name(metro, id));   /* 换乘站加粗 */
        else
            printf("%s", station_name(metro, id));
    }
    printf("\n");
}

void render_all_lines(const Metro *metro) {
    for (size_t i = 0; i < metro->lines.rows.size; i++)
        render_line(&metro->lines.rows.items[i], metro);
}

void render_route(const Route *route, const Metro *metro) {
    printf("路线：");
    for (size_t i = 0; i < route->stations.size; i++) {
        if (i > 0)
            printf(" → ");
        printf("%s", station_name(metro, route->stations.items[i]));
    }
    printf("\n");

    for (size_t i = 0; i < route->edges_ids.size; i++) {
        Edge *e = edge_find_by_id(&metro->edges, route->edges_ids.items[i]);
        Line *ln = line_find_by_id(&metro->lines, e->line_id);
        printf("  %s → %s  （%s）\n",
               station_name(metro, e->from_station_id),
               station_name(metro, e->to_station_id),
               ln != NULL ? ln->name : "?");
    }

    for (size_t i = 0; i < route->transfers.size; i++) {
        int sid = route->transfers.items[i];
        printf("  ⚠ 在 %s 站换乘\n", station_name(metro, sid));
    }

    printf("统计：%d 站 / %.1f 公里 / %d 分钟\n",
           route->total_stations,
           route->total_meters / 1000.0,
           (route->total_seconds + 30) / 60);
}