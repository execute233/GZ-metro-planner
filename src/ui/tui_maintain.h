#ifndef GZMP_TUI_MAINTAIN_H
#define GZMP_TUI_MAINTAIN_H
#include "tui_state.h"
void tui_form_dispose(MaintainForm *form);
void tui_maintain_event(TuiState *s, TuiEvent event);
void tui_maintain_render(const TuiState *s, CellSurface *surface);
#endif
