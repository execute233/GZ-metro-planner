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

static int station_field(Maintenance *edit, const Metro *metro) {
    const char *input = edit->input;
    Station *station = &edit->station;
    if (edit->step == 0) {
        const Station *existing = station_find_by_name(&metro->stations, input);
        if (!*input || (existing && existing->id != station->id))
            return fail(edit, "站名不能为空或与已有站点重名");
        if (text_field(station->name, sizeof(station->name), input))
            return fail(edit, "站名过长，请缩短后重试");
    } else if (edit->step == 1 || edit->step == 2) {
        if ((!*input && edit->action == 1) ||
            text_field(edit->step == 1 ? station->pinyin_name : station->initials,
                       STATION_PINYIN_MAX, input))
            return fail(edit, "请输入长度合适的拼音或首字母");
    } else if (edit->step == 3) {
        if (text_field(station->en_name, sizeof(station->en_name), input))
            return fail(edit, "英文名过长");
    } else {
        double x, y;
        if (pair(input, &x, &y) || x < 0 || y < 0 || x > 4096 || y > 4096)
            return fail(edit, "坐标应为 0–4096 范围内的 x,y");
        if (edit->action == 5) {
            for (size_t i = 0; i < metro->edges.rows.size; i++) {
                const Edge *edge = &metro->edges.rows.items[i];
                int other = edge->from_station_id == station->id ? edge->to_station_id :
                            edge->to_station_id == station->id ? edge->from_station_id : 0;
                const Station *neighbor = station_find_by_id(&metro->stations, other);
                if (neighbor && neighbor->x == x && neighbor->y == y)
                    return fail(edit, "坐标不能与相邻站点重合");
            }
        }
        station->x = x;
        station->y = y;
        edit->confirm = 1;
        return 0;
    }
    edit->step++;
    return 0;
}

static int interval_cost(Maintenance *edit, Edge *edge) {
    double seconds, meters;
    if (pair(edit->input, &seconds, &meters) || seconds < 0 || seconds > 100000 ||
        meters < 1 || meters > 100000 || floor(seconds) != seconds || floor(meters) != meters)
        return fail(edit, "秒数应为 0–100000 整数，米数为 1–100000 整数");
    edge->cost_time_second = (int)seconds;
    edge->cost_meters = (int)meters;
    return 0;
}

static int line_field(Maintenance *edit, const Metro *metro) {
    const char *input = edit->input;
    Line *line = &edit->line;
    if (edit->step == 0) {
        const Line *existing = line_find_by_name(&metro->lines, input);
        if (!*input || (existing && existing->id != line->id))
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
        if (edit->action == 3)
            line->color = 37;
        edit->step++;
    } else if (edit->action == 6) {
        if (text_field(line->en_name, sizeof(line->en_name), input))
            return fail(edit, "英文名过长");
        edit->confirm = 1;
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
        Edge edge = {.from_station_id = line->station_ids.items[edit->interval],
                     .to_station_id = line->station_ids.items[edit->interval + 1]};
        if (interval_cost(edit, &edge))
            return -1;
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

static int validate_interval(Maintenance *edit, const Metro *metro, int from, int to) {
    if (!line_find_by_id(&metro->lines, edit->line.id))
        return fail(edit, "未找到线路");
    const Station *a = station_find_by_id(&metro->stations, from);
    const Station *b = station_find_by_id(&metro->stations, to);
    if (!a || !b)
        return fail(edit, "未找到站点");
    if (from == to)
        return fail(edit, "起点和终点不能相同");
    if (!isfinite(a->x) || !isfinite(a->y) || !isfinite(b->x) || !isfinite(b->y) ||
        a->x < 0 || a->x > 4096 || a->y < 0 || a->y > 4096 ||
        b->x < 0 || b->x > 4096 || b->y < 0 || b->y > 4096 ||
        (a->x == b->x && a->y == b->y))
        return fail(edit, "区间端点需要不同的有效地图位置");
    for (size_t i = 0; i < metro->edges.rows.size; i++) {
        const Edge *edge = &metro->edges.rows.items[i];
        if (edge->line_id == edit->line.id &&
            ((edge->from_station_id == from && edge->to_station_id == to) ||
             (edge->from_station_id == to && edge->to_station_id == from)))
            return fail(edit, "该线路已存在两站间区间（含反向）");
    }
    return 0;
}

static int interval_field(Maintenance *edit, const Metro *metro) {
    if (edit->step == 0) {
        const Line *line = line_find_by_name(&metro->lines, edit->input);
        if (!line)
            return fail(edit, "未找到线路，请输入完整线路名");
        edit->line.id = line->id;
        strcpy(edit->line.name, line->name);
    } else if (edit->step < 3) {
        const Station *station = station_find_by_name(&metro->stations, edit->input);
        if (!station)
            return fail(edit, "未找到站点，请输入完整站名");
        if (edit->step == 2) {
            int from = edit->line.station_ids.items[0], to = station->id;
            if (edit->action == 7) {
                if (validate_interval(edit, metro, from, to))
                    return -1;
            } else {
                const Edge *selected = NULL;
                for (size_t i = 0; i < metro->edges.rows.size; i++) {
                    const Edge *edge = &metro->edges.rows.items[i];
                    if (edge->line_id == edit->line.id &&
                        ((edge->from_station_id == from && edge->to_station_id == to) ||
                         (edge->from_station_id == to && edge->to_station_id == from))) {
                        selected = edge;
                        break;
                    }
                }
                if (!selected)
                    return fail(edit, "该线路不存在两站间的直接区间");
                edit->original_edge = *selected;
            }
        }
        if (al_int_push(&edit->line.station_ids, station->id))
            return fail(edit, "内存不足");
        if (edit->action == 9 && edit->step == 2)
            edit->confirm = 1;
    } else if (edit->action == 7) {
        /* Two selected stations use the same interval form as a new line. */
        return line_field(edit, metro);
    } else if (edit->action == 8) {
        Edge edge = edit->original_edge;
        if (interval_cost(edit, &edge))
            return -1;
        if (al_edge_push(&edit->edges.rows, edge))
            return fail(edit, "内存不足");
        edit->confirm = 1;
    } else if (edit->step == 3) {
        const Station *station = station_find_by_name(&metro->stations, edit->input);
        if (!station)
            return fail(edit, "未找到插入站点，请先添加站点");
        if (validate_interval(edit, metro, edit->line.station_ids.items[0], station->id) ||
            validate_interval(edit, metro, station->id, edit->line.station_ids.items[1]))
            return -1;
        edit->station = *station;
    } else {
        Edge edge = {.line_id = edit->line.id,
                     .from_station_id = edit->step == 4 ? edit->line.station_ids.items[0] : edit->station.id,
                     .to_station_id = edit->step == 4 ? edit->station.id : edit->line.station_ids.items[1]};
        if (interval_cost(edit, &edge))
            return -1;
        if (al_edge_push(&edit->edges.rows, edge))
            return fail(edit, "内存不足");
        if (edit->step == 5)
            edit->confirm = 1;
    }
    edit->step++;
    return 0;
}

static void prefill(Maintenance *edit) {
    if (edit->confirm)
        return;
    if (edit->action == 8 && edit->step == 3) {
        snprintf(edit->input, sizeof(edit->input), "%d,%d",
                 edit->original_edge.cost_time_second, edit->original_edge.cost_meters);
    } else if (edit->action == 5 && edit->station.id) {
        const Station *station = &edit->station;
        const char *fields[] = {station->name, station->pinyin_name, station->initials, station->en_name};
        if (edit->step < 4)
            snprintf(edit->input, sizeof(edit->input), "%s", fields[edit->step]);
        else
            snprintf(edit->input, sizeof(edit->input), "%.17g,%.17g", station->x, station->y);
    } else if (edit->action == 6 && edit->line.id) {
        if (edit->step == 1)
            snprintf(edit->input, sizeof(edit->input), "%06X", edit->line.rgb);
        else
            snprintf(edit->input, sizeof(edit->input), "%s",
                     edit->step == 0 ? edit->line.name : edit->line.en_name);
    }
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
    if (edit->action == 5 && !edit->station.id) {
        const Station *station = station_find_by_name(&metro->stations, edit->input);
        if (!station)
            return fail(edit, "未找到站点");
        edit->station = *station;
    } else if (edit->action == 6 && !edit->line.id) {
        const Line *line = line_find_by_name(&metro->lines, edit->input);
        if (!line)
            return fail(edit, "未找到线路");
        edit->line = *line;
        /* Only metadata is edited; the draft must not own the live membership. */
        edit->line.station_ids = (ArrayList_Int){0};
    } else if (edit->action == 1 || edit->action == 5)
        result = station_field(edit, metro);
    else if (edit->action == 3 || edit->action == 6)
        result = line_field(edit, metro);
    else if (edit->action >= 7)
        result = interval_field(edit, metro);
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
    if (!result) {
        edit->input[0] = 0;
        prefill(edit);
    }
    return result;
}

static int save_existing_interval(Maintenance *edit, Metro *metro) {
    const Edge *original = &edit->original_edge;
    Edge *current = edge_find_by_id(&metro->edges, original->id);
    if (!current || current->line_id != original->line_id ||
        current->from_station_id != original->from_station_id ||
        current->to_station_id != original->to_station_id ||
        current->cost_time_second != original->cost_time_second ||
        current->cost_meters != original->cost_meters)
        return fail(edit, "原区间已变化，请取消后重新选择");
    if (edit->action == 8) {
        if (edit->edges.rows.size != 1)
            return -1;
        current->cost_time_second = edit->edges.rows.items[0].cost_time_second;
        current->cost_meters = edit->edges.rows.items[0].cost_meters;
        return 0;
    }
    if (edit->action == 10) {
        if (edit->edges.rows.size != 2)
            return -1;
        for (size_t i = 0; i < 2; i++) {
            Edge edge = edit->edges.rows.items[i];
            if (validate_interval(edit, metro, edge.from_station_id, edge.to_station_id) ||
                edge_add(&metro->edges, &edge))
                return -1;
        }
    }
    /* Allocate replacement IDs before removing the old edge to avoid reusing its ID. */
    return edge_remove(&metro->edges, original->id);
}

int maintenance_save(Maintenance *edit, const char *path) {
    Metro metro = {0};
    int result = -1;
    edit->message[0] = 0;
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
    } else if (edit->action == 5) {
        Station *station = station_find_by_id(&metro.stations, edit->station.id);
        const Station *existing = station_find_by_name(&metro.stations, edit->station.name);
        if (!station || (existing && existing->id != station->id))
            goto done;
        *station = edit->station;
    } else if (edit->action == 6) {
        Line *line = line_find_by_id(&metro.lines, edit->line.id);
        const Line *existing = line_find_by_name(&metro.lines, edit->line.name);
        if (!line || (existing && existing->id != line->id))
            goto done;
        strcpy(line->name, edit->line.name);
        strcpy(line->en_name, edit->line.en_name);
        line->rgb = edit->line.rgb;
    } else if (edit->action == 7) {
        if (edit->edges.rows.size != 1)
            goto done;
        Edge edge = edit->edges.rows.items[0];
        edge.line_id = edit->line.id;
        if (validate_interval(edit, &metro, edge.from_station_id, edge.to_station_id) ||
            edge_add(&metro.edges, &edge))
            goto done;
    } else if (edit->action >= 8 && edit->action <= 10) {
        if (save_existing_interval(edit, &metro))
            goto done;
    } else
        goto done;
    result = metro_io_save(path, &metro);
done:
    station_table_dispose(&metro.stations);
    line_table_dispose(&metro.lines);
    edge_table_dispose(&metro.edges);
    if (result && !edit->message[0])
        fail(edit, "保存失败：检查输入、数据库写权限或占用情况；原数据未改动");
    return result;
}
