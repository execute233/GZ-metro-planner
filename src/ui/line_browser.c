#include "line_browser.h"
#include "../algo/line_paths.h"
#include "../render/line_diagram.h"
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
        line_browser_close(b);
    } else if (key == LINE_TAB) {
        b->focus_left = !b->focus_left;
    } else if (delta) {
        int *position = b->focus_left ? &b->scroll : &b->selected;
        int before = *position;
        int max = b->focus_left ? b->row_count - page : (int)metro->lines.rows.size - 1;
        if (max < 0)
            max = 0;
        *position += delta;
        if (*position < 0)
            *position = 0;
        if (*position > max)
            *position = max;
        if (!b->focus_left && before != b->selected)
            b->scroll = 0;
    }
}

void line_browser_frame(LineBrowser *b, const Metro *metro, MapFrame *f) {
    int page = f->height - 6;
    int sidebar = f->width >= 80 ? 24 : 16;
    int left = f->width - sidebar - 1;
    char value[160];
    for (int y = 0; y < f->height - 3; y++)
        frame_glyph(f, left, y, 0x2502, 0, 0);
    frame_text(f, left + 2, 0, sidebar - 2, b->focus_left ? "线路目录" : "> 线路目录", 1, 0);
    int start = b->selected / page * page;
    for (int i = start; i < (int)metro->lines.rows.size && i < start + page; i++) {
        const Line *line = &metro->lines.rows.items[i];
        snprintf(value, sizeof(value), "%s %s", i == b->selected ? ">" : " ", line->name);
        frame_text(f, left + 1, 3 + i - start, sidebar - 1, value, 4 + line->id, 0);
    }
    if (metro->lines.rows.size) {
        const Line *line = &metro->lines.rows.items[b->selected];
        if (b->line_id != line->id) {
            al_int_dispose(&b->paths);
            b->line_id = line->id;
            b->scroll = 0;
            b->failed = line_paths(metro, line->id, &b->paths) != 0;
        }
        b->row_count = b->failed ? 0 : line_diagram_render(f, metro, line->id, &b->paths, left, b->scroll);
        int max = b->row_count > page ? b->row_count - page : 0;
        if (b->scroll > max) {
            b->scroll = max;
            /* Width changes can reflow branches, rings and disconnected parts. */
            for (int y = 3; y < f->height - 3; y++)
                for (int x = 0; x < left; x++)
                    f->cells[y * f->width + x] = (MapCell){0};
            line_diagram_render(f, metro, line->id, &b->paths, left, b->scroll);
        }
        snprintf(value, sizeof(value), "%s%s", b->focus_left ? "> " : "", line->name);
        frame_text(f, 2, 0, left - 3, value, 4 + line->id, 0);
        frame_text(f, 2, 1, left - 3, "● 普通站  ◆ 换乘站", 0, 1);
        if (b->failed || b->row_count < 0)
            frame_text(f, 2, 3, left - 3, "绘制失败，请退出重试", 0, 0);
        else if (!b->paths.size)
            frame_text(f, 2, 3, left - 3, "此线路暂无区间", 0, 0);
    } else {
        b->row_count = 0;
        frame_text(f, 2, 3, left - 3, "图集暂无线路", 0, 0);
    }
    snprintf(value, sizeof(value), "%s  %s · %zu 条线路",
             b->scroll ? "↑ 上方还有" : "已到顶部",
             b->scroll + page < b->row_count ? "↓ 下方还有" : "已到底部", metro->lines.rows.size);
    frame_text(f, 1, f->height - 3, f->width - 2, value, 0, 1);
    frame_text(f, 1, f->height - 2, f->width - 2,
               b->focus_left ? "↑↓ 滚动线路图  PgUp/PgDn 翻页" : "↑↓ 选择线路并预览  PgUp/PgDn 翻页", 0, 0);
    frame_text(f, 1, f->height - 1, f->width - 2, "Tab 切换左右焦点  Esc 返回地图", 0, 0);
}
