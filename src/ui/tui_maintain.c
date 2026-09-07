#include "tui_maintain.h"
#include "../render/utf8.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *actions[] = {"新增站点", "删除站点", "新增线路", "删除线路"};
void tui_form_dispose(MaintainForm *f) {
    al_int_dispose(&f->line.station_ids); al_edge_dispose(&f->edges); *f = (MaintainForm){0};
}
static void clear_input(MaintainForm *f) { f->input[0] = 0; f->cursor = 0; f->candidate = 0; }
static bool valid_text(const char *text, size_t cap, bool required) {
    if ((required && !*text) || strlen(text) >= cap) return false;
    return strpbrk(text, ",;\r\n\t") == NULL;
}
static int number(const char *text, int minimum, int *value) {
    char *end; errno = 0; long n = strtol(text, &end, 10);
    if (errno || end == text || *end || n < minimum || n > 10000000) return -1;
    *value = (int)n; return 0;
}
static int chosen_line(const TuiState *s) {
    int n = 0;
    for (size_t i = 0; i < s->metro.lines.rows.size; i++) {
        const Line *l = &s->metro.lines.rows.items[i];
        if (strstr(l->name, s->form.input) && n++ == s->form.candidate) return l->id;
    }
    return 0;
}
static bool referenced(const Metro *m, int id) {
    for (size_t i = 0; i < m->lines.rows.size; i++) for (size_t j = 0; j < m->lines.rows.items[i].station_ids.size; j++)
        if (m->lines.rows.items[i].station_ids.items[j] == id) return true;
    for (size_t i = 0; i < m->edges.rows.size; i++) if (m->edges.rows.items[i].from_station_id == id || m->edges.rows.items[i].to_station_id == id) return true;
    return false;
}
static int apply_form(TuiState *s, Metro *m, MapDocument *map) {
    MaintainForm *f = &s->form;
    if (f->action == MAINT_ADD_STATION) {
        Station st = f->station;
        if (station_find_by_name(&m->stations, st.name)) return -1;
        if (station_add(&m->stations, &st)) return -1;
        return al_map_station_push(&map->stations, (MapStation){st.id, f->point, 2, 0, false});
    }
    if (f->action == MAINT_DELETE_STATION) {
        if (referenced(m, f->target_id)) {
            snprintf(s->status, sizeof(s->status), "站点仍被线路或区间引用，不能删除"); return -1;
        }
        for (size_t i = 0; i < map->stations.size; i++) if (map->stations.items[i].station_id == f->target_id) { al_map_station_remove_at(&map->stations, i); break; }
        return station_remove(&m->stations, f->target_id);
    }
    if (f->action == MAINT_ADD_LINE) {
        Line l = f->line;
        if (line_find_by_name(&m->lines, l.name) || line_add(&m->lines, &l)) return -1;
        if (al_map_style_push(&map->styles, (MapStyle){l.id, f->rgb})) return -1;
        for (size_t i = 0; i < f->edges.size; i++) {
            Edge e = f->edges.items[i]; e.line_id = l.id;
            if (edge_add(&m->edges, &e)) return -1;
            const MapStation *a = map_station(map, e.from_station_id), *b = map_station(map, e.to_station_id);
            if (a && b && (al_map_vertex_push(&map->vertices, (MapVertex){e.id, 0, a->point}) || al_map_vertex_push(&map->vertices, (MapVertex){e.id, 1, b->point}))) return -1;
        }
        return 0;
    }
    for (size_t i = 0; i < m->edges.rows.size;) {
        Edge e = m->edges.rows.items[i];
        if (e.line_id != f->target_id) { i++; continue; }
        for (size_t j = 0; j < map->vertices.size;) {
            if (map->vertices.items[j].edge_id == e.id) al_map_vertex_remove_at(&map->vertices, j); else j++;
        }
        edge_remove(&m->edges, e.id);
    }
    for (size_t i = 0; i < map->styles.size;) {
        if (map->styles.items[i].line_id == f->target_id) al_map_style_remove_at(&map->styles, i); else i++;
    }
    return line_remove(&m->lines, f->target_id);
}
static void save_form(TuiState *s) {
    Metro m = {0}; MapDocument map = {0};
    snprintf(s->status, sizeof(s->status), "操作失败：重复名称、引用或内存不足");
    if (!project_clone(&m, &s->metro) && !map_clone(&map, &s->map) && !apply_form(s, &m, &map)) {
        if (!tui_commit(s, &m, &map)) tui_form_dispose(&s->form);
    }
    project_dispose(&m); map_dispose(&map);
}
static void station_step(TuiState *s) {
    MaintainForm *f = &s->form;
    char *dest = f->step == 0 ? f->station.name : f->step == 1 ? f->station.pinyin_name : f->station.en_name;
    size_t cap = f->step == 0 ? sizeof(f->station.name) : f->step == 1 ? sizeof(f->station.pinyin_name) : sizeof(f->station.en_name);
    if (!valid_text(f->input, cap, f->step == 0)) { snprintf(s->status, sizeof(s->status), "文本过长、为空或包含 CSV 分隔符"); return; }
    if (f->step == 0 && station_find_by_name(&s->metro.stations, f->input)) { snprintf(s->status, sizeof(s->status), "站名已存在"); return; }
    strcpy(dest, f->input); f->step++; clear_input(f);
    if (f->step == 3) { s->page = PAGE_PICK; f->point = (MapPoint){s->view.center_x, s->view.center_y}; }
}
static void line_step(TuiState *s) {
    MaintainForm *f = &s->form;
    if (f->step == 0) {
        if (!valid_text(f->input, sizeof(f->line.name), true) || line_find_by_name(&s->metro.lines, f->input)) goto invalid;
        strcpy(f->line.name, f->input); f->step = 1;
    } else if (f->step == 1) {
        const char *color = f->input[0] == '#' ? f->input+1 : f->input;
        if (strlen(color) != 6 || strspn(color, "0123456789abcdefABCDEF") != 6) goto invalid;
        f->rgb = (uint32_t)strtoul(color, NULL, 16); f->line.color = 37; f->step = 2;
    } else if (f->step == 2) {
        if (!*f->input && f->line.station_ids.size >= 2) { f->step = 3; }
        else {
            int id = tui_search_station(&s->metro, f->input, f->candidate, NULL);
            if (!id) goto invalid;
            for (size_t i = 0; i < f->line.station_ids.size; i++) if (f->line.station_ids.items[i] == id) goto invalid;
            if (al_int_push(&f->line.station_ids, id)) goto invalid;
        }
    } else {
        char *comma = strchr(f->input, ',');
        if (!comma) goto invalid;
        *comma = 0;
        Edge edge = {0};
        int bad = number(f->input, 0, &edge.cost_time_second) || number(comma+1, 1, &edge.cost_meters);
        *comma = ',';
        if (bad) goto invalid;
        size_t i = f->edges.size;
        edge.from_station_id = f->line.station_ids.items[i]; edge.to_station_id = f->line.station_ids.items[i+1];
        if (al_edge_push(&f->edges, edge)) goto invalid;
        if (f->edges.size+1 == f->line.station_ids.size) { s->page = PAGE_CONFIRM; return; }
    }
    clear_input(f); return;
invalid: snprintf(s->status, sizeof(s->status), "输入非法、站点重复或名称已存在，请修改");
}
void tui_maintain_event(TuiState *s, TuiEvent e) {
    MaintainForm *f = &s->form;
    if (e.type == EVENT_KEY && e.key == TKEY_ESCAPE) {
        tui_form_dispose(f); s->page = PAGE_MAP; s->focus = 0; return;
    }
    if (s->page == PAGE_MAINTAIN) {
        if (e.type != EVENT_KEY) return;
        if (e.key == TKEY_UP) s->menu = (s->menu+3)%4;
        if (e.key == TKEY_DOWN || e.key == TKEY_TAB) s->menu = (s->menu+1)%4;
        if (e.key == TKEY_ENTER) {
            if (!s->layout_valid) { snprintf(s->status, sizeof(s->status), "布局损坏，维护已禁用；修复 CSV 后重启"); return; }
            tui_form_dispose(f); f->action = (MaintainAction)s->menu; s->page = PAGE_FORM;
        }
        return;
    }
    if (s->page == PAGE_CONFIRM) {
        if (e.type == EVENT_KEY && e.key == TKEY_ENTER) save_form(s);
        return;
    }
    if (s->page == PAGE_PICK) {
        double step = 4/s->view.zoom;
        if (e.type == EVENT_KEY) {
            if (e.key == TKEY_UP) f->point.y -= step;
            if (e.key == TKEY_DOWN) f->point.y += step;
            if (e.key == TKEY_LEFT) f->point.x -= step;
            if (e.key == TKEY_RIGHT) f->point.x += step;
            if (e.key == TKEY_ENTER) s->page = PAGE_CONFIRM;
        }
        if (e.type == EVENT_MOUSE && e.x >= 0 && e.x < s->view.cols && e.y >= 1 && e.y <= s->view.rows) {
            if (e.wheel) viewport_zoom_at(&s->view, e.wheel > 0 ? 1.25 : 0.8, (MapPoint){e.x*2, (e.y-1)*4});
            if (e.click) f->point = viewport_unproject(&s->view, (MapPoint){e.x*2, (e.y-1)*4});
        }
        return;
    }
    if (e.type == EVENT_KEY && (e.key == TKEY_DOWN || e.key == TKEY_UP)) {
        int count = 0;
        if (f->action == MAINT_DELETE_LINE) {
            for (size_t i = 0; i < s->metro.lines.rows.size; i++) if (strstr(s->metro.lines.rows.items[i].name, f->input)) count++;
        } else tui_search_station(&s->metro, f->input, 0, &count);
        if (e.key == TKEY_DOWN && f->candidate+1 < count) f->candidate++;
        if (e.key == TKEY_UP && f->candidate) f->candidate--;
    } else if (e.type == EVENT_KEY && e.key == TKEY_ENTER) {
        if (f->action == MAINT_ADD_STATION) station_step(s);
        else if (f->action == MAINT_ADD_LINE) line_step(s);
        else {
            f->target_id = f->action == MAINT_DELETE_LINE ? chosen_line(s) : tui_search_station(&s->metro, f->input, f->candidate, NULL);
            if (f->target_id) s->page = PAGE_CONFIRM;
        }
    } else { tui_edit(f->input, sizeof(f->input), &f->cursor, e); f->candidate = 0; }
}
static void row(CellSurface *s, int y, const char *text) { surface_text(s, 3, y, s->cols-6, text, 0xd8dee9); }
void tui_maintain_render(const TuiState *s, CellSurface *out) {
    const MaintainForm *f = &s->form; char buf[256];
    row(out, 1, "系统维护 | Esc 取消并返回地图");
    if (s->page == PAGE_MAINTAIN) {
        for (int i = 0; i < 4; i++) { snprintf(buf, sizeof(buf), "%s %s", i == s->menu ? ">" : " ", actions[i]); row(out, 4+i*2, buf); }
        return;
    }
    row(out, 3, actions[f->action]);
    if (s->page == PAGE_CONFIRM) {
        const char *name = f->action == MAINT_ADD_STATION ? f->station.name : f->action == MAINT_ADD_LINE ? f->line.name :
            f->action == MAINT_DELETE_LINE ? line_find_by_id(&s->metro.lines, f->target_id)->name : station_find_by_id(&s->metro.stations, f->target_id)->name;
        snprintf(buf, sizeof(buf), "确认%s：%s", actions[f->action], name); row(out, 6, buf);
        row(out, 8, "Enter 保存；Esc 取消。删除线路同时删除其区间。"); return;
    }
    const char *prompt;
    if (f->action == MAINT_ADD_STATION) prompt = f->step == 0 ? "中文站名" : f->step == 1 ? "拼音名（可留空）" : "英文名（可留空）";
    else if (f->action == MAINT_ADD_LINE) prompt = f->step == 0 ? "线路名称" : f->step == 1 ? "颜色 RGB，例如 #F3D03E" : f->step == 2 ? "搜索站点，Enter 追加；空输入 Enter 完成站序" : "区间运行秒数,里程米数（例如 120,1500）";
    else prompt = "搜索要删除的名称；方向键选候选，Enter 确认";
    row(out, 5, prompt);
    snprintf(buf, sizeof(buf), "> %s", f->input); row(out, 7, buf);
    if (f->action == MAINT_ADD_LINE && f->step == 3) {
        size_t i = f->edges.size;
        snprintf(buf, sizeof(buf), "%s → %s", station_find_by_id(&s->metro.stations, f->line.station_ids.items[i])->name, station_find_by_id(&s->metro.stations, f->line.station_ids.items[i+1])->name);
        row(out, 9, buf);
    }
    if (f->action == MAINT_DELETE_STATION || (f->action == MAINT_ADD_LINE && f->step == 2)) {
        int id = tui_search_station(&s->metro, f->input, f->candidate, NULL);
        const Station *st = station_find_by_id(&s->metro.stations, id);
        snprintf(buf, sizeof(buf), "候选：%s | 已选 %zu 站", st ? st->name : "无", f->line.station_ids.size); row(out, 10, buf);
    }
    if (f->action == MAINT_DELETE_LINE) {
        const Line *l = line_find_by_id(&s->metro.lines, chosen_line(s));
        snprintf(buf, sizeof(buf), "候选：%s", l ? l->name : "无"); row(out, 10, buf);
    }
    row(out, 12, "Enter 下一步 | ← → 编辑 | ↑ ↓ 选择候选");
}
