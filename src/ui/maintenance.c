#include "maintenance.h"
#include "../io/metro_io.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void maintenance_close(Maintenance *edit) {
    al_int_dispose(&edit->line.station_ids);
    edge_table_dispose(&edit->edges);
    memset(edit, 0, sizeof(*edit));
}

static int fail(Maintenance *edit, const char *message) {
    snprintf(edit->message, sizeof(edit->message), "%s", message);
    return -1;
}

static int text_field(char *dst, size_t capacity, const char *src) {
    if (strlen(src) >= capacity)
        return -1;
    strcpy(dst, src);
    return 0;
}

static int pair(const char *input, double *a, double *b) {
    char *end;
    errno = 0;
    *a = strtod(input, &end);
    if (end == input || *end != ',')
        return -1;
    input = end + 1;
    *b = strtod(input, &end);
    return errno || end == input || *end || !isfinite(*a) || !isfinite(*b) ? -1 : 0;
}

static int referenced(const Metro *metro, int id) {
    for (size_t i = 0; i < metro->edges.rows.size; i++) {
        const Edge *edge = &metro->edges.rows.items[i];
        if (edge->from_station_id == id || edge->to_station_id == id)
            return 1;
    }
    return 0;
}

const char *maintenance_prompt(const Maintenance *edit) {
    if (edit->confirm)
        return "输入 y 保存，n 返回维护菜单";
    if (!edit->action)
        return "输入操作编号 1–4";
    if (edit->action == 2)
        return "要删除的站点全名";
    if (edit->action == 4)
        return "要删除的线路全名";
    if (edit->action == 1) {
        const char *prompts[] = {"新站点全名", "全拼", "搜索首字母", "英文名（可留空）",
                                 "示意图坐标 x,y（范围 0–4096）"};
        return prompts[edit->step];
    }
    const char *prompts[] = {"新线路名称", "线路颜色 RRGGBB（如 FF8800）",
                             "依次输入站点全名，空行结束", "区间秒数,米数（如 60,1000）"};
    return prompts[edit->step];
}

static int station_field(Maintenance *edit, const Metro *metro) {
    const char *input = edit->input;
    Station *station = &edit->station;
    if (edit->step == 0) {
        if (!*input || station_find_by_name(&metro->stations, input))
            return fail(edit, "站名不能为空或与已有站点重名");
        if (text_field(station->name, sizeof(station->name), input))
            return fail(edit, "站名过长，请缩短后重试");
    } else if (edit->step == 1 || edit->step == 2) {
        if (!*input || text_field(edit->step == 1 ? station->pinyin_name : station->initials,
                                  STATION_PINYIN_MAX, input))
            return fail(edit, "请输入长度合适的拼音或首字母");
    } else if (edit->step == 3) {
        if (text_field(station->en_name, sizeof(station->en_name), input))
            return fail(edit, "英文名过长");
    } else {
        if (pair(input, &station->x, &station->y) || station->x < 0 || station->y < 0 ||
            station->x > 4096 || station->y > 4096)
            return fail(edit, "坐标应为 0–4096 范围内的 x,y");
        edit->confirm = 1;
        return 0;
    }
    edit->step++;
    return 0;
}

static int line_field(Maintenance *edit, const Metro *metro) {
    const char *input = edit->input;
    Line *line = &edit->line;
    if (edit->step == 0) {
        if (!*input || line_find_by_name(&metro->lines, input))
            return fail(edit, "线路名不能为空或与已有线路重名");
        if (text_field(line->name, sizeof(line->name), input))
            return fail(edit, "线路名过长");
        edit->step++;
    } else if (edit->step == 1) {
        char *end;
        errno = 0;
        unsigned long rgb = strtoul(input, &end, 16);
        if (errno || strlen(input) != 6 || *end || rgb > 0xffffff)
            return fail(edit, "请输入六位十六进制颜色 RRGGBB");
        line->rgb = (unsigned)rgb;
        line->color = 37;
        edit->step++;
    } else if (edit->step == 2) {
        if (!*input) {
            if (line->station_ids.size < 2)
                return fail(edit, "至少需要两个已有站点");
            edit->step++;
            return 0;
        }
        Station *station = station_find_by_name(&metro->stations, input);
        if (!station)
            return fail(edit, "未找到站点，请输入完整站名");
        for (size_t i = 0; i < line->station_ids.size; i++)
            if (line->station_ids.items[i] == station->id)
                return fail(edit, "站点已在这条线路中");
        if (al_int_push(&line->station_ids, station->id))
            return fail(edit, "内存不足");
    } else {
        double seconds, meters;
        if (pair(input, &seconds, &meters) || seconds < 0 || seconds > 100000 ||
            meters < 1 || meters > 100000 || floor(seconds) != seconds || floor(meters) != meters)
            return fail(edit, "秒数应为 0–100000 整数，米数为 1–100000 整数");
        Edge edge = {.from_station_id = line->station_ids.items[edit->interval],
                     .to_station_id = line->station_ids.items[edit->interval + 1],
                     .cost_time_second = (int)seconds, .cost_meters = (int)meters};
        const Station *a = station_find_by_id(&metro->stations, edge.from_station_id);
        const Station *b = station_find_by_id(&metro->stations, edge.to_station_id);
        if (!a || !b || (a->x == b->x && a->y == b->y))
            return fail(edit, "区间端点需要不同的有效地图位置");
        if (al_edge_push(&edit->edges.rows, edge))
            return fail(edit, "内存不足");
        if (++edit->interval + 1 == line->station_ids.size)
            edit->confirm = 1;
    }
    return 0;
}

int maintenance_submit(Maintenance *edit, const Metro *metro) {
    edit->message[0] = 0;
    if (edit->confirm) {
        if (!strcmp(edit->input, "y") || !strcmp(edit->input, "Y"))
            return 1;
        if (!strcmp(edit->input, "n") || !strcmp(edit->input, "N")) {
            maintenance_close(edit);
            edit->active = 1;
            return 0;
        }
        return fail(edit, "输入 y 保存或 n 取消");
    }
    int result = 0;
    if (!edit->action) {
        if (strlen(edit->input) != 1 || edit->input[0] < '1' || edit->input[0] > '4')
            return fail(edit, "请选择 1 添加站点 / 2 删除站点 / 3 添加线路 / 4 删除线路");
        edit->action = edit->input[0] - '0';
    } else if (edit->action == 1)
        result = station_field(edit, metro);
    else if (edit->action == 3)
        result = line_field(edit, metro);
    else if (edit->action == 2) {
        Station *station = station_find_by_name(&metro->stations, edit->input);
        if (!station)
            return fail(edit, "未找到站点");
        if (referenced(metro, station->id))
            return fail(edit, "该站仍被区间引用，请先删除相关线路");
        edit->station = *station;
        edit->confirm = 1;
    } else {
        Line *line = line_find_by_name(&metro->lines, edit->input);
        if (!line)
            return fail(edit, "未找到线路");
        edit->line.id = line->id;
        strcpy(edit->line.name, line->name);
        edit->confirm = 1;
    }
    if (!result)
        edit->input[0] = 0;
    return result;
}

int maintenance_save(Maintenance *edit, const char *path) {
    Metro metro = {0};
    int result = -1;
    if (!edit->confirm || metro_io_load(path, &metro))
        return fail(edit, "数据库载入失败，未保存");
    if (edit->action == 1) {
        if (station_find_by_name(&metro.stations, edit->station.name) ||
            station_add(&metro.stations, &edit->station))
            goto done;
    } else if (edit->action == 2) {
        Station *station = station_find_by_name(&metro.stations, edit->station.name);
        if (!station || referenced(&metro, station->id) || station_remove(&metro.stations, station->id))
            goto done;
    } else if (edit->action == 3) {
        if (line_find_by_name(&metro.lines, edit->line.name) || line_add(&metro.lines, &edit->line))
            goto done;
        for (size_t i = 0; i < edit->edges.rows.size; i++) {
            Edge edge = edit->edges.rows.items[i];
            edge.line_id = edit->line.id;
            if (edge_add(&metro.edges, &edge))
                goto done;
        }
    } else if (edit->action == 4) {
        Line *line = line_find_by_name(&metro.lines, edit->line.name);
        if (!line)
            goto done;
        int id = line->id;
        for (size_t i = 0; i < metro.edges.rows.size;) {
            if (metro.edges.rows.items[i].line_id == id)
                edge_remove(&metro.edges, metro.edges.rows.items[i].id);
            else
                i++;
        }
        if (line_remove(&metro.lines, id))
            goto done;
    } else
        goto done;
    result = metro_io_save(path, &metro);
done:
    station_table_dispose(&metro.stations);
    line_table_dispose(&metro.lines);
    edge_table_dispose(&metro.edges);
    if (result)
        fail(edit, "保存失败：检查输入、数据库写权限或占用情况；原数据未改动");
    return result;
}
