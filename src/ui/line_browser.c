#include "line_browser.h"
#include "../algo/line_paths.h"
#include <stdio.h>
#include <string.h>

void line_browser_close(LineBrowser *b) {
    al_int_dispose(&b->paths);
    memset(b, 0, sizeof(*b));
}

void line_browser_key(LineBrowser *b, const Metro *metro, LineBrowserKey key, int height) {
    int page = height > 6 ? height - 6 : 1;
    int delta = key == LINE_UP ? -1 : key == LINE_DOWN ? 1 :
                key == LINE_PAGE_UP ? -page : key == LINE_PAGE_DOWN ? page : 0;
    if (key == LINE_BACK) {
        if (b->detail) {
            al_int_dispose(&b->paths);
            b->detail = b->scroll = b->failed = 0;
        } else {
            line_browser_close(b);
        }
    } else if (key == LINE_ENTER && !b->detail && metro->lines.rows.size) {
        b->detail = 1;
        b->scroll = 0;
        b->row_count = 0;
        b->failed = line_paths(metro, metro->lines.rows.items[b->selected].id, &b->paths) != 0;
    } else if (delta) {
        int *position = b->detail ? &b->scroll : &b->selected;
        int max = b->detail ? b->row_count - page : (int)metro->lines.rows.size - 1;
        if (max < 0)
            max = 0;
        *position += delta;
        if (*position < 0)
            *position = 0;
        if (*position > max)
            *position = max;
    }
}

/* Wrap by display cells, retaining every UTF-8 code point and scrollable row. */
static void text_rows(LineBrowser *b, MapFrame *f, int *row, const char *text, int color) {
    int x = 2;
    uint32_t cp;
    while (utf8_next(&text, &cp)) {
        int width = unicode_width(cp);
        if (x + width > f->width - 2) {
            (*row)++;
            x = 2;
        }
        int y = 3 + *row - b->scroll;
        if (y >= 3 && y < f->height - 3)
            frame_glyph(f, x, y, cp, color, 0);
        x += width;
    }
    (*row)++;
}

static int detail_rows(LineBrowser *b, const Metro *metro, MapFrame *f, int line_id) {
    int row = 0, group = 0, first = 0;
    char text[160];
    if (b->failed || !b->paths.size) {
        text_rows(b, f, &row, b->failed ? "无法载入线路，请返回目录重试" : "此线路暂无区间", 0);
        return row;
    }
    for (size_t i = 0; i < b->paths.size; i++) {
        int id = b->paths.items[i];
        if (!id)
            continue;
        int start = !i || !b->paths.items[i - 1];
        if (start) {
            first = id;
            snprintf(text, sizeof(text), "── 站段 %d ──", ++group);
            text_rows(b, f, &row, text, 0);
        }
        const Station *station = station_find_by_id(&metro->stations, id);
        snprintf(text, sizeof(text), "%s%s%s", start ? "   " : "→ ", station->name,
                 !start && id == first ? "（回到起始站，闭合）" : "");
        text_rows(b, f, &row, text, 4 + line_id);
        for (size_t j = 0; j < metro->lines.rows.size; j++) {
            const Line *line = &metro->lines.rows.items[j];
            if (line->id == line_id)
                continue;
            for (size_t k = 0; k < metro->edges.rows.size; k++) {
                const Edge *e = &metro->edges.rows.items[k];
                if (e->line_id == line->id && (e->from_station_id == id || e->to_station_id == id)) {
                    snprintf(text, sizeof(text), "    换乘：%s", line->name);
                    text_rows(b, f, &row, text, 4 + line->id);
                    break;
                }
            }
        }
    }
    return row;
}

void line_browser_frame(LineBrowser *b, const Metro *metro, MapFrame *f) {
    int page = f->height - 6;
    char text[160];
    if (b->detail) {
        const Line *line = &metro->lines.rows.items[b->selected];
        b->row_count = detail_rows(b, metro, f, line->id);
        int max = b->row_count > page ? b->row_count - page : 0;
        if (b->scroll > max) {
            b->scroll = max;
            frame_clear(f);
            detail_rows(b, metro, f, line->id);
        }
        snprintf(text, sizeof(text), "线路详情 · %s", line->name);
        frame_text(f, 2, 0, f->width - 4, text, 4 + line->id, 0);
        frame_text(f, 2, 1, f->width - 4, "站段独立列出；分岔站可重复出现", 0, 1);
        snprintf(text, sizeof(text), "第 %d–%d / %d 行", b->scroll + 1,
                 b->scroll + page < b->row_count ? b->scroll + page : b->row_count, b->row_count);
    } else {
        frame_text(f, 2, 0, f->width - 4, "广州地铁 · 线路目录", 1, 0);
        int start = b->selected / page * page;
        for (int i = start; i < (int)metro->lines.rows.size && i < start + page; i++) {
            const Line *line = &metro->lines.rows.items[i];
            snprintf(text, sizeof(text), "%s %s", i == b->selected ? ">" : " ", line->name);
            frame_text(f, 2, 3 + i - start, f->width - 4, text, 4 + line->id, 0);
        }
        if (!metro->lines.rows.size)
            frame_text(f, 2, 3, f->width - 4, "图集暂无线路", 0, 0);
        snprintf(text, sizeof(text), "共 %zu 条线路", metro->lines.rows.size);
    }
    frame_text(f, 2, f->height - 3, f->width - 4, text, 0, 1);
    frame_text(f, 2, f->height - 2, f->width - 4, "↑↓ 移动  PgUp/PgDn 翻页  Enter 查看", 0, 0);
    frame_text(f, 2, f->height - 1, f->width - 4, b->detail ? "Esc 返回线路目录" : "Esc 返回地图", 0, 0);
}
