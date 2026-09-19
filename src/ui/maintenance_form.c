#include "maintenance_form.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *actions[] = {"添加站点", "删除站点", "添加线路", "删除线路及区间",
    "修改站点", "修改线路信息", "添加线路区间", "修改区间参数", "删除区间", "区间插站"};

void maintenance_form_close(MaintenanceForm *f) {
    free(f->fields);
    memset(f, 0, sizeof(*f));
}

void maintenance_form_open(MaintenanceForm *f, int selected) {
    maintenance_form_close(f);
    f->active = 1;
    f->selected = selected;
}

static int allocate_fields(MaintenanceForm *f, int count) {
    MaintenanceField *fields = calloc((size_t)count, sizeof(*fields));
    if (!fields) {
        snprintf(f->message, sizeof(f->message), "内存不足，请重试");
        return -1;
    }
    free(f->fields);
    f->fields = fields;
    f->count = count;
    f->focus = f->scroll = 0;
    f->cursor = 0;
    f->message[0] = 0;
    return 0;
}

static void field(MaintenanceForm *f, int i, const char *label, FormFieldKind kind,
                  size_t limit, int required, const char *value) {
    MaintenanceField *v = &f->fields[i];
    snprintf(v->label, sizeof(v->label), "%s", label);
    snprintf(v->value, sizeof(v->value), "%s", value ? value : "");
    v->kind = kind;
    v->limit = limit;
    v->required = required;
}

static void line_labels(MaintenanceForm *f) {
    for (int i = 0; i < f->station_count; i++) {
        MaintenanceField *v = &f->fields[2 + i];
        snprintf(v->label, sizeof(v->label), "站点 %d 全名", i + 1);
        v->kind = FORM_STATION_REF;
        v->limit = STATION_NAME_MAX;
        v->required = 1;
    }
    for (int i = 0; i < f->station_count - 1; i++) {
        MaintenanceField *v = &f->fields[2 + f->station_count + i];
        snprintf(v->label, sizeof(v->label), "站点 %d→%d 秒数,米数", i + 1, i + 2);
        v->kind = FORM_COST;
        v->limit = 128;
        v->required = 1;
    }
}

static int begin(MaintenanceForm *f) {
    int a = f->selected + 1;
    int count = a == 1 ? 5 : a == 3 ? 5 : a <= 6 ? 1 : a == 7 ? 4 : a == 10 ? 6 : 3;
    if (allocate_fields(f, count)) return -1;
    f->action = a;
    f->loaded = 0;
    f->station_count = a == 3 ? 2 : 0;
    if (a == 1) {
        field(f, 0, "站点全名", FORM_STATION_NAME, STATION_NAME_MAX, 1, NULL);
        field(f, 1, "全拼", FORM_TEXT, STATION_PINYIN_MAX, 1, NULL);
        field(f, 2, "搜索首字母", FORM_TEXT, STATION_PINYIN_MAX, 1, NULL);
        field(f, 3, "英文名（可留空）", FORM_TEXT, STATION_EN_MAX, 0, NULL);
        field(f, 4, "示意图坐标 x,y", FORM_COORDS, 128, 1, NULL);
    } else if (a == 3) {
        field(f, 0, "线路名称", FORM_LINE_NAME, LINE_NAME_MAX, 1, NULL);
        field(f, 1, "颜色 RRGGBB", FORM_COLOR, 7, 1, NULL);
        line_labels(f);
    } else if (a <= 6) {
        int station = a == 2 || a == 5;
        field(f, 0, station ? "站点全名" : "线路全名",
              station ? FORM_STATION_REF : FORM_LINE_REF,
              station ? STATION_NAME_MAX : LINE_NAME_MAX, 1, NULL);
    } else {
        field(f, 0, "所属线路全名", FORM_LINE_REF, LINE_NAME_MAX, 1, NULL);
        field(f, 1, "起点站全名", FORM_STATION_REF, STATION_NAME_MAX, 1, NULL);
        field(f, 2, "终点站全名", FORM_STATION_REF, STATION_NAME_MAX, 1, NULL);
        if (a == 7)
            field(f, 3, "秒数,米数", FORM_COST, 128, 1, NULL);
        if (a == 10) {
            field(f, 3, "插入站全名（须已存在）", FORM_STATION_REF, STATION_NAME_MAX, 1, NULL);
            field(f, 4, "起点→插入站 秒数,米数", FORM_COST, 128, 1, NULL);
            field(f, 5, "插入站→终点 秒数,米数", FORM_COST, 128, 1, NULL);
        }
    }
    return 0;
}

/* Focus rows contain fields, per-station remove buttons, then form buttons. */
static int field_row(const MaintenanceForm *f, int i) {
    if (f->action != 3 || i < 2) return i;
    if (i < 2 + f->station_count) return 2 + 2 * (i - 2);
    return i + f->station_count;
}

static int focused_field(const MaintenanceForm *f) {
    for (int i = 0; i < f->count; i++)
        if (field_row(f, i) == f->focus) return i;
    return -1;
}

static int buttons_row(const MaintenanceForm *f) {
    return f->count + (f->action == 3 ? f->station_count : 0);
}

static int last_row(const MaintenanceForm *f) {
    return buttons_row(f) + (f->action == 3 ? 2 : 1);
}

static void cursor_end(MaintenanceForm *f) {
    int i = focused_field(f);
    f->cursor = i < 0 ? 0 : strlen(f->fields[i].value);
}

static void mark(MaintenanceForm *f, int i, const char *error) {
    snprintf(f->fields[i].error, sizeof(f->fields[i].error), "%s", error);
}

static int number_pair(const char *s, double *a, double *b) {
    char *end;
    errno = 0;
    *a = strtod(s, &end);
    if (end == s || *end != ',') return -1;
    s = end + 1;
    *b = strtod(s, &end);
    return errno || end == s || *end || !isfinite(*a) || !isfinite(*b) ? -1 : 0;
}

static int validate_fields(MaintenanceForm *f, const Metro *metro) {
    int errors = 0;
    for (int i = 0; i < f->count; i++) {
        MaintenanceField *v = &f->fields[i];
        const char *s = v->value;
        v->error[0] = 0;
        double a, b;
        if (v->required && !*s) mark(f, i, "此项不能为空");
        else if (strlen(s) >= v->limit) mark(f, i, "内容过长，请缩短后重试");
        else if (v->kind == FORM_STATION_REF && !station_find_by_name(&metro->stations, s))
            mark(f, i, "未找到站点，请输入完整站名");
        else if (v->kind == FORM_LINE_REF && !line_find_by_name(&metro->lines, s))
            mark(f, i, "未找到线路，请输入完整线路名");
        else if (v->kind == FORM_COLOR && (strlen(s) != 6 || strspn(s, "0123456789abcdefABCDEF") != 6))
            mark(f, i, "请输入六位十六进制颜色，如 FF8800");
        else if (v->kind == FORM_COORDS &&
                 (number_pair(s, &a, &b) || a < 0 || a > 4096 || b < 0 || b > 4096))
            mark(f, i, "坐标应为 0–4096 范围内的 x,y");
        else if (v->kind == FORM_COST &&
                 (number_pair(s, &a, &b) || a < 0 || a > 100000 || b < 1 || b > 100000 ||
                  floor(a) != a || floor(b) != b))
            mark(f, i, "秒数为 0–100000 整数，米数为 1–100000 整数");
        else if (v->kind == FORM_STATION_NAME) {
            const Station *existing = station_find_by_name(&metro->stations, s);
            const Station *target = f->loaded ? station_find_by_name(&metro->stations, f->target) : NULL;
            if (existing && existing != target) mark(f, i, "站名与已有站点重名");
        } else if (v->kind == FORM_LINE_NAME) {
            const Line *existing = line_find_by_name(&metro->lines, s);
            const Line *target = f->loaded ? line_find_by_name(&metro->lines, f->target) : NULL;
            if (existing && existing != target) mark(f, i, "线路名与已有线路重名");
        }
        if (*v->error) errors++;
    }
    return errors;
}

static int invalid(MaintenanceForm *f) {
    for (int i = 0; i < f->count; i++) {
        if (*f->fields[i].error) {
            f->focus = field_row(f, i);
            cursor_end(f);
            break;
        }
    }
    snprintf(f->message, sizeof(f->message), "校验失败：请修改红色字段，再确认");
    return 0;
}

static int feed(MaintenanceForm *f, Maintenance *draft, const Metro *metro, int i) {
    snprintf(draft->input, sizeof(draft->input), "%s", f->fields[i].value);
    if (maintenance_submit(draft, metro) < 0) {
        mark(f, i, draft->message);
        return -1;
    }
    return 0;
}

static int load_target(MaintenanceForm *f, const Metro *metro, Maintenance *draft) {
    char target[128];
    snprintf(target, sizeof(target), "%s", f->fields[0].value);
    if (f->action == 5) {
        const Station *s = station_find_by_name(&metro->stations, target);
        if (allocate_fields(f, 5)) return 0;
        field(f, 0, "站点全名", FORM_STATION_NAME, STATION_NAME_MAX, 1, s->name);
        field(f, 1, "全拼", FORM_TEXT, STATION_PINYIN_MAX, 0, s->pinyin_name);
        field(f, 2, "搜索首字母", FORM_TEXT, STATION_PINYIN_MAX, 0, s->initials);
        field(f, 3, "英文名（可留空）", FORM_TEXT, STATION_EN_MAX, 0, s->en_name);
        char coords[128];
        snprintf(coords, sizeof(coords), "%.17g,%.17g", s->x, s->y);
        field(f, 4, "示意图坐标 x,y", FORM_COORDS, 128, 1, coords);
    } else if (f->action == 6) {
        const Line *line = line_find_by_name(&metro->lines, target);
        if (allocate_fields(f, 3)) return 0;
        field(f, 0, "线路名称", FORM_LINE_NAME, LINE_NAME_MAX, 1, line->name);
        char color[16];
        snprintf(color, sizeof(color), "%06X", line->rgb);
        field(f, 1, "颜色 RRGGBB", FORM_COLOR, 7, 1, color);
        field(f, 2, "英文名（可留空）", FORM_TEXT, LINE_EN_MAX, 0, line->en_name);
    } else {
        for (int i = 0; i < 3; i++)
            if (feed(f, draft, metro, i)) return invalid(f);
        MaintenanceField *fields = realloc(f->fields, 4 * sizeof(*fields));
        if (!fields) {
            snprintf(f->message, sizeof(f->message), "内存不足，请重试");
            return 0;
        }
        f->fields = fields;
        f->count = 4;
        memset(&f->fields[3], 0, sizeof(f->fields[3]));
        field(f, 3, "秒数,米数", FORM_COST, 128, 1, draft->input);
        f->focus = 3;
    }
    snprintf(f->target, sizeof(f->target), "%s", target);
    f->loaded = 1;
    cursor_end(f);
    return 0;
}

static int confirm(MaintenanceForm *f, const Metro *metro, Maintenance *draft) {
    f->message[0] = 0;
    if (validate_fields(f, metro)) return invalid(f);
    maintenance_close(draft);
    draft->active = 1;
    draft->action = f->action;
    if ((f->action == 5 || f->action == 6 || f->action == 8) && !f->loaded)
        return load_target(f, metro, draft);
    if (f->loaded && (f->action == 5 || f->action == 6)) {
        snprintf(draft->input, sizeof(draft->input), "%s", f->target);
        if (maintenance_submit(draft, metro) < 0) {
            mark(f, 0, draft->message);
            return invalid(f);
        }
    }
    int end = f->action == 3 ? 2 + f->station_count : f->count;
    for (int i = 0; i < end; i++)
        if (feed(f, draft, metro, i)) return invalid(f);
    if (f->action == 3) {
        draft->input[0] = 0;
        if (maintenance_submit(draft, metro) < 0) {
            mark(f, 2, draft->message);
            return invalid(f);
        }
        for (int i = end; i < f->count; i++)
            if (feed(f, draft, metro, i)) return invalid(f);
    }
    return draft->confirm ? 1 : 0;
}

static void resize_line(MaintenanceForm *f, int remove) {
    int old_count = f->station_count, n = old_count + (remove < 0 ? 1 : -1);
    if (n < 2 || n >= MAP_LIMIT) {
        snprintf(f->message, sizeof(f->message), "线路至少保留两个站点，最多 %d 个", MAP_LIMIT - 1);
        return;
    }
    MaintenanceField *old = f->fields;
    MaintenanceField *fields = calloc((size_t)(2 * n + 1), sizeof(*fields));
    if (!fields) {
        snprintf(f->message, sizeof(f->message), "内存不足，请重试");
        return;
    }
    fields[0] = old[0]; fields[1] = old[1];
    for (int i = 0; i < n; i++) {
        int source = remove >= 0 && i >= remove ? i + 1 : i;
        if (source < old_count) fields[2 + i] = old[2 + source];
    }
    for (int i = 0; i < n - 1; i++) {
        if (remove > 0 && remove < old_count - 1 && i == remove - 1) continue;
        int source = remove >= 0 && i >= remove ? i + 1 : i;
        if (source < old_count - 1) fields[2 + n + i] = old[2 + old_count + source];
    }
    free(old);
    f->fields = fields;
    f->count = 2 * n + 1;
    f->station_count = n;
    line_labels(f);
    for (int i = 0; i < f->count; i++) f->fields[i].error[0] = 0;
    f->focus = field_row(f, 2 + (remove < 0 ? n - 1 : remove < n ? remove : n - 1));
    cursor_end(f);
    snprintf(f->message, sizeof(f->message), "%s", remove < 0 ? "已添加站点行" : "已删除站点行；新相邻区间需重新填写参数");
}

static size_t previous(const char *s, size_t cursor) {
    if (cursor) cursor--;
    while (cursor && ((unsigned char)s[cursor] & 0xc0) == 0x80) cursor--;
    return cursor;
}

static size_t following(const char *s, size_t cursor) {
    if (s[cursor]) cursor++;
    while (((unsigned char)s[cursor] & 0xc0) == 0x80) cursor++;
    return cursor;
}

int maintenance_form_key(MaintenanceForm *f, MaintenanceFormKey key,
                         const Metro *metro, Maintenance *draft) {
    if (key == FORM_BACK) {
        maintenance_close(draft);
        if (f->loaded) { begin(f); return 0; }
        if (f->action) maintenance_form_open(f, f->selected);
        else maintenance_form_close(f);
        return 0;
    }
    if (!f->action) {
        if (key == FORM_UP && f->selected > 0) f->selected--;
        if (key == FORM_DOWN && f->selected < MAINTENANCE_ACTION_COUNT - 1) f->selected++;
        if (key == FORM_ENTER) begin(f);
        return 0;
    }
    int i = focused_field(f);
    if (key == FORM_UP || key == FORM_DOWN || (key == FORM_ENTER && i >= 0)) {
        int delta = key == FORM_UP ? -1 : 1;
        if (f->focus + delta >= 0 && f->focus + delta <= last_row(f)) f->focus += delta;
        cursor_end(f);
    } else if (key == FORM_ENTER) {
        int buttons = buttons_row(f);
        if (f->focus == last_row(f)) return maintenance_form_key(f, FORM_BACK, metro, draft);
        if (f->action == 3 && f->focus < buttons) resize_line(f, (f->focus - 3) / 2);
        else if (f->action == 3 && f->focus == buttons) resize_line(f, -1);
        else return confirm(f, metro, draft);
    } else if (i >= 0) {
        char *s = f->fields[i].value;
        if (key == FORM_LEFT) f->cursor = previous(s, f->cursor);
        if (key == FORM_RIGHT) f->cursor = following(s, f->cursor);
        if (key == FORM_HOME) f->cursor = 0;
        if (key == FORM_END) f->cursor = strlen(s);
        size_t start = f->cursor, end = f->cursor;
        if (key == FORM_BACKSPACE) start = previous(s, start);
        if (key == FORM_DELETE) end = following(s, end);
        if (start != end) {
            memmove(s + start, s + end, strlen(s + end) + 1);
            f->cursor = start;
        }
    }
    return 0;
}

void maintenance_form_type(MaintenanceForm *f, unsigned cp) {
    int i = focused_field(f);
    if (!f->action || i < 0 || cp < 32 || cp == 127 || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return;
    char bytes[4]; size_t n;
    if (cp < 0x80) { bytes[0] = (char)cp; n = 1; }
    else if (cp < 0x800) {
        bytes[0] = (char)(0xc0 | (cp >> 6)); bytes[1] = (char)(0x80 | (cp & 63)); n = 2;
    } else if (cp < 0x10000) {
        bytes[0] = (char)(0xe0 | (cp >> 12)); bytes[1] = (char)(0x80 | ((cp >> 6) & 63));
        bytes[2] = (char)(0x80 | (cp & 63)); n = 3;
    } else {
        bytes[0] = (char)(0xf0 | (cp >> 18)); bytes[1] = (char)(0x80 | ((cp >> 12) & 63));
        bytes[2] = (char)(0x80 | ((cp >> 6) & 63)); bytes[3] = (char)(0x80 | (cp & 63)); n = 4;
    }
    char *s = f->fields[i].value;
    size_t len = strlen(s);
    if (len + n >= sizeof(f->fields[i].value)) {
        snprintf(f->message, sizeof(f->message), "输入已达长度上限");
        return;
    }
    memmove(s + f->cursor + n, s + f->cursor, len - f->cursor + 1);
    memcpy(s + f->cursor, bytes, n);
    f->cursor += n;
}

static void draw_field(MaintenanceForm *f, MapFrame *frame, int i, int y) {
    MaintenanceField *v = &f->fields[i];
    int focused = field_row(f, i) == f->focus;
    int color = *v->error ? 2 : focused ? 1 : 0;
    char text[300];
    snprintf(text, sizeof(text), "%s %s", focused ? ">" : " ", v->label);
    frame_text(frame, 2, y, frame->width - 4, text, color, 0);
    size_t start = 0;
    if (focused) {
        size_t p = f->cursor;
        int columns = 0;
        while (p) {
            size_t prev = previous(v->value, p);
            const char *s = v->value + prev;
            uint32_t cp;
            utf8_next(&s, &cp);
            columns += unicode_width(cp);
            if (columns >= frame->width - 9) { start = p; break; }
            p = prev;
        }
        snprintf(text, sizeof(text), "%.*s|%s", (int)(f->cursor - start), v->value + start, v->value + f->cursor);
    } else snprintf(text, sizeof(text), "%s", v->value);
    frame_text(frame, 4, y + 1, frame->width - 6, *text ? text : "[空]", color, 0);
}

void maintenance_form_frame(MaintenanceForm *f, MapFrame *frame) {
    int width = frame->width - 4;
    frame_text(frame, 2, 0, width, f->action ? actions[f->action - 1] : "广州地铁 · 地图维护", 1, 0);
    if (!f->action) {
        for (int i = 0; i < MAINTENANCE_ACTION_COUNT; i++) {
            char text[100];
            snprintf(text, sizeof(text), "%s %s", i == f->selected ? ">" : " ", actions[i]);
            frame_text(frame, 2, 2 + i, width, text, i == f->selected, 0);
        }
        frame_text(frame, 2, frame->height - 3, width, f->message, 0, 0);
        frame_text(frame, 2, frame->height - 1, width, "↑↓ 选择  Enter 打开  Esc 返回地图", 0, 0);
        return;
    }
    const char *hint = f->action == 3 ? "站点按列表顺序连接；新增区间绘制为直线" :
        f->action == 9 ? "删除该线区间，保留站点；可能断开线路" :
        f->action == 4 ? "删除线路及其全部区间；保留站点" :
        f->action == 10 ? "原区间将替换为两段直线；分别填写参数" :
        f->action == 5 ? "修改坐标会将关联区间重绘为直线" : "填写后选择确认；一次校验并保存";
    frame_text(frame, 2, 1, width, hint, 0, 1);
    int visible = (frame->height - 7) / 2;
    if (visible < 1) visible = 1;
    if (f->focus < f->scroll) f->scroll = f->focus;
    if (f->focus >= f->scroll + visible) f->scroll = f->focus - visible + 1;
    int max_scroll = last_row(f) - visible + 1;
    if (max_scroll < 0) max_scroll = 0;
    if (f->scroll > max_scroll) f->scroll = max_scroll;
    for (int row = f->scroll; row <= last_row(f) && row < f->scroll + visible; row++) {
        int y = 3 + 2 * (row - f->scroll), i;
        for (i = 0; i < f->count; i++) if (field_row(f, i) == row) break;
        if (i < f->count) { draw_field(f, frame, i, y); continue; }
        const char *label = row == last_row(f) ? "取消" : "确认";
        char text[128];
        if (f->action == 3 && row < buttons_row(f)) {
            snprintf(text, sizeof(text), "%s [删除站点 %d 行]", row == f->focus ? ">" : " ", (row - 3) / 2 + 1);
        } else {
            if (f->action == 3 && row == buttons_row(f)) label = "添加站点行";
            if (row != last_row(f) && !f->loaded && (f->action == 5 || f->action == 6 || f->action == 8)) label = "载入对象";
            snprintf(text, sizeof(text), "%s [%s]", row == f->focus ? ">" : " ", label);
        }
        frame_text(frame, 2, y, width, text, row == f->focus, 0);
    }
    int i = focused_field(f);
    const char *message = i >= 0 && *f->fields[i].error ? f->fields[i].error : f->message;
    frame_text(frame, 2, frame->height - 3, width, message, 2, 0);
    char position[128];
    snprintf(position, sizeof(position), "%d/%d  ↑↓/Tab 切换  ←→ 编辑  Enter 下一项/按钮", f->focus + 1, last_row(f) + 1);
    frame_text(frame, 2, frame->height - 2, width, position, 0, 1);
    frame_text(frame, 2, frame->height - 1, width, "Esc 放弃并返回上一级  Home/End 行首尾", 0, 0);
}
