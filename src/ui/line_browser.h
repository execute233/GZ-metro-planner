#ifndef GZMP_LINE_BROWSER_H
#define GZMP_LINE_BROWSER_H
#include "../render/map_render.h"

typedef struct {
    int active, detail, selected, scroll, row_count;
    ArrayList_Int paths;
    int failed;
} LineBrowser;

typedef enum { LINE_UP, LINE_DOWN, LINE_PAGE_UP, LINE_PAGE_DOWN, LINE_ENTER, LINE_BACK } LineBrowserKey;
void line_browser_key(LineBrowser *b, const Metro *metro, LineBrowserKey key, int height);
void line_browser_frame(LineBrowser *b, const Metro *metro, MapFrame *frame);
void line_browser_close(LineBrowser *b);
#endif
