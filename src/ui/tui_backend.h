#ifndef GZMP_TUI_BACKEND_H
#define GZMP_TUI_BACKEND_H
#include "../render/canvas.h"
typedef enum { EVENT_NONE, EVENT_TEXT, EVENT_KEY, EVENT_MOUSE, EVENT_RESIZE, EVENT_QUIT } EventType;
typedef enum { TKEY_UP, TKEY_DOWN, TKEY_LEFT, TKEY_RIGHT, TKEY_TAB, TKEY_ENTER, TKEY_ESCAPE, TKEY_BACKSPACE, TKEY_DELETE, TKEY_HOME, TKEY_END, TKEY_PAGEUP, TKEY_PAGEDOWN } TuiKey;
typedef struct { EventType type; uint32_t text; TuiKey key; int x, y, wheel; bool click; } TuiEvent;
int tui_backend_init(void);
void tui_backend_dispose(void);
void tui_backend_size(int *cols, int *rows);
TuiEvent tui_backend_event(void);
void tui_backend_present(const CellSurface *surface);
#endif
