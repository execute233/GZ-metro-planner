#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../io/metro_io.h"
#include "../algo/graph.h"
#include "../algo/router.h"
#include "../render/render.h"

/* 数据目录（由 ui_main_loop 设置，维护保存用） */
static const char *g_data_dir = "data";

int ui_read_line(char *buf, size_t size) {
    if (fgets(buf, (int)size, stdin) == NULL)
        return -1;
    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r' ||
                       buf[len - 1] == ' ' || buf[len - 1] == '\t'))
        buf[--len] = '\0';
    char *p = buf;
    while (*p == ' ' || *p == '\t')
        p++;
    if (p != buf)
        memmove(buf, p, strlen(p) + 1);
    return 0;
}

void ui_plan_route(Metro *metro) {
    char buf[128];
    printf("起点站：");
    if (ui_read_line(buf, sizeof(buf)) != 0)
        return;
    Station *from = station_find_by_name(&metro->stations, buf);
    printf("终点站：");
    if (ui_read_line(buf, sizeof(buf)) != 0)
        return;
    Station *to = station_find_by_name(&metro->stations, buf);

    if (from == NULL || to == NULL) {
        printf("起点或终点站不存在\n");
        return;
    }
    printf("规划目标：1 最少站点  2 最短路程  3 最少时间\n请选择：");
    if (ui_read_line(buf, sizeof(buf)) != 0)
        return;
    int choice = atoi(buf);
    RouteMetric metric;
    switch (choice) {
        case 1:  metric = ROUTE_MIN_STATIONS; break;
        case 2:  metric = ROUTE_MIN_DISTANCE; break;
        case 3:  metric = ROUTE_MIN_TIME;     break;
        default: printf("无效选择\n"); return;
    }

    Graph g;
    if (graph_build(&g, metro) != 0) {
        printf("图构建失败\n");
        return;
    }
    Route route;
    route_init(&route);
    if (router_find_route(&g, metro, from->id, to->id, metric, &route) == 0)
        render_route(&route, metro);
    else
        printf("两站之间没有可达路径\n");
    route_dispose(&route);
    graph_dispose(&g);
}

/* 站是否仍被线路或边引用 */
static int station_referenced(const Metro *metro, int station_id) {
    for (size_t i = 0; i < metro->lines.rows.size; i++) {
        const ArrayList_Int *ids = &metro->lines.rows.items[i].station_ids;
        for (size_t k = 0; k < ids->size; k++) {
            if (ids->items[k] == station_id)
                return 1;
        }
    }
    for (size_t i = 0; i < metro->edges.rows.size; i++) {
        const Edge *e = &metro->edges.rows.items[i];
        if (e->from_station_id == station_id || e->to_station_id == station_id)
            return 1;
    }
    return 0;
}

static void ui_add_station(Metro *metro) {
    char buf[128];
    Station st;
    memset(&st, 0, sizeof(st));
    printf("站名：");
    if (ui_read_line(buf, sizeof(buf)) != 0 || buf[0] == '\0')
        return;
    strncpy(st.name, buf, STATION_NAME_MAX - 1);
    printf("拼音名：");
    if (ui_read_line(buf, sizeof(buf)) != 0)
        return;
    strncpy(st.pinyin_name, buf, STATION_PINYIN_MAX - 1);
    printf("英文名：");
    if (ui_read_line(buf, sizeof(buf)) != 0)
        return;
    strncpy(st.en_name, buf, STATION_EN_MAX - 1);

    if (station_find_by_name(&metro->stations, st.name) != NULL) {
        printf("站点已存在\n");
        return;
    }
    if (station_add(&metro->stations, &st) == 0) {
        metro_io_save(g_data_dir, metro);
        printf("已添加站点 %s（id=%d）\n", st.name, st.id);
    } else {
        printf("添加失败\n");
    }
}

static void ui_del_station(Metro *metro) {
    char buf[128];
    printf("要删除的站名：");
    if (ui_read_line(buf, sizeof(buf)) != 0)
        return;
    Station *st = station_find_by_name(&metro->stations, buf);
    if (st == NULL) {
        printf("站点不存在\n");
        return;
    }
    if (station_referenced(metro, st->id)) {
        printf("该站仍被线路或区间引用，拒绝删除\n");
        return;
    }
    if (station_remove(&metro->stations, st->id) == 0) {
        metro_io_save(g_data_dir, metro);
        printf("已删除站点 %s\n", buf);
    } else {
        printf("删除失败\n");
    }
}

/* 交互式收集一条线的站序，返回 0 成功 / -1 取消 */
static int collect_station_ids(Metro *metro, ArrayList_Int *ids) {
    char buf[128];
    printf("依次输入站名，空行结束：\n");
    for (;;) {
        printf("  > ");
        if (ui_read_line(buf, sizeof(buf)) != 0)
            return -1;
        if (buf[0] == '\0')
            break;
        Station *st = station_find_by_name(&metro->stations, buf);
        if (st == NULL) {
            printf("  站点 %s 不存在，请先添加\n", buf);
            return -1;
        }
        for (size_t i = 0; i < ids->size; i++) {
            if (ids->items[i] == st->id) {
                printf("  站点重复\n");
                return -1;
            }
        }
        al_int_push(ids, st->id);
    }
    return ids->size >= 2 ? 0 : -1;
}

static void ui_add_line(Metro *metro) {
    char buf[128];
    Line ln;
    memset(&ln, 0, sizeof(ln));
    al_int_init(&ln.station_ids);

    printf("线路名：");
    if (ui_read_line(buf, sizeof(buf)) != 0 || buf[0] == '\0') {
        al_int_dispose(&ln.station_ids);
        return;
    }
    strncpy(ln.name, buf, LINE_NAME_MAX - 1);
    if (line_find_by_name(&metro->lines, ln.name) != NULL) {
        printf("线路已存在\n");
        al_int_dispose(&ln.station_ids);
        return;
    }
    printf("颜色号（ANSI 前景色，如 31 红 / 34 蓝 / 33 黄）：");
    if (ui_read_line(buf, sizeof(buf)) != 0) {
        al_int_dispose(&ln.station_ids);
        return;
    }
    ln.color = atoi(buf);

    if (collect_station_ids(metro, &ln.station_ids) != 0) {
        printf("取消添加（需至少 2 个已存在站点）\n");
        al_int_dispose(&ln.station_ids);
        return;
    }

    line_add(&metro->lines, &ln);
    for (size_t i = 0; i + 1 < ln.station_ids.size; i++) {
        Edge e;
        memset(&e, 0, sizeof(e));
        e.line_id = ln.id;
        e.from_station_id = ln.station_ids.items[i];
        e.to_station_id = ln.station_ids.items[i + 1];
        printf("区间 %s→%s 运行秒数与里程(米)（逗号分隔）：",
               station_find_by_id(&metro->stations, e.from_station_id)->name,
               station_find_by_id(&metro->stations, e.to_station_id)->name);
        if (ui_read_line(buf, sizeof(buf)) != 0) {
            line_remove(&metro->lines, ln.id);
            al_int_dispose(&ln.station_ids);
            return;
        }
        char *comma = strchr(buf, ',');
        if (comma == NULL) {
            line_remove(&metro->lines, ln.id);
            printf("格式错误，已回滚\n");
            al_int_dispose(&ln.station_ids);
            return;
        }
        *comma = '\0';
        e.cost_time_second = atoi(buf);
        e.cost_meters = atoi(comma + 1);
        if (e.cost_meters <= 0) {
            line_remove(&metro->lines, ln.id);
            printf("里程非法，已回滚\n");
            al_int_dispose(&ln.station_ids);
            return;
        }
        edge_add(&metro->edges, &e);
    }
    al_int_dispose(&ln.station_ids);
    metro_io_save(g_data_dir, metro);
    printf("已添加线路 %s（id=%d，%d 个区间）\n", ln.name, ln.id,
           (int)metro->edges.rows.size);
}

static void ui_del_line(Metro *metro) {
    char buf[128];
    printf("要删除的线路名：");
    if (ui_read_line(buf, sizeof(buf)) != 0)
        return;
    Line *ln = line_find_by_name(&metro->lines, buf);
    if (ln == NULL) {
        printf("线路不存在\n");
        return;
    }
    for (size_t i = 0; i < metro->edges.rows.size;) {
        if (metro->edges.rows.items[i].line_id == ln->id)
            edge_remove(&metro->edges, metro->edges.rows.items[i].id);
        else
            i++;
    }
    line_remove(&metro->lines, ln->id);
    metro_io_save(g_data_dir, metro);
    printf("已删除线路 %s 及其区间\n", buf);
}

void ui_maintain(Metro *metro) {
    char buf[16];
    for (;;) {
        printf("\n维护：1 添加站点  2 删除站点  3 添加线路  4 删除线路  5 返回\n请选择：");
        if (ui_read_line(buf, sizeof(buf)) != 0)
            return;
        switch (atoi(buf)) {
            case 1: ui_add_station(metro); break;
            case 2: ui_del_station(metro); break;
            case 3: ui_add_line(metro);    break;
            case 4: ui_del_line(metro);    break;
            case 5: return;
            default: printf("无效选择\n"); break;
        }
    }
}

int ui_main_loop(const char *data_dir) {
    g_data_dir = data_dir;
    Metro metro;
    station_table_init(&metro.stations);
    line_table_init(&metro.lines);
    edge_table_init(&metro.edges);
    if (metro_io_load(data_dir, &metro) != 0) {
        fprintf(stderr, "载入数据失败，请检查 %s 下的三个 CSV 文件\n", data_dir);
        station_table_dispose(&metro.stations);
        line_table_dispose(&metro.lines);
        edge_table_dispose(&metro.edges);
        return -1;
    }

    char buf[16];
    for (;;) {
        printf("\n广州地铁乘车路线规划系统\n");
        printf("1 显示全部线路\n2 规划路线\n3 系统维护\n4 退出\n请选择：");
        if (ui_read_line(buf, sizeof(buf)) != 0)
            break;
        switch (atoi(buf)) {
            case 1: render_all_lines(&metro); break;
            case 2: ui_plan_route(&metro);    break;
            case 3: ui_maintain(&metro);      break;
            case 4: return 0;
            default: printf("无效选择\n");    break;
        }
    }

    station_table_dispose(&metro.stations);
    line_table_dispose(&metro.lines);
    edge_table_dispose(&metro.edges);
    return 0;
}