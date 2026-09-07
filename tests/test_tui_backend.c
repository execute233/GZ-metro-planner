#include "../src/ui/tui_backend.h"
#include <windows.h>
#include <stdio.h>
/* Run in a real console/ConPTY. Kept separate from redirected CTest tests. */
int main(void) {
    DWORD input, output, after_input, after_output;
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE), out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleMode(in,&input) || !GetConsoleMode(out,&output)) {
        fprintf(stderr,"Run test_tui_backend in a Windows terminal.\n"); return 77;
    }
    UINT input_cp=GetConsoleCP(), output_cp=GetConsoleOutputCP();
    if (tui_backend_init()) return 1;
    int cols,rows; tui_backend_size(&cols,&rows);
    CellSurface surface={0};
    if (surface_resize(&surface,cols,rows)) { tui_backend_dispose(); return 1; }
    surface_clear(&surface);
    surface_text(&surface,0,0,cols,"广州地铁 ⣿ ─ │ Unicode / RGB / cleanup",0x88c0d0);
    tui_backend_present(&surface); surface_dispose(&surface);
    tui_backend_dispose();
    int ok=GetConsoleMode(in,&after_input) && GetConsoleMode(out,&after_output) &&
        input==after_input && output==after_output && input_cp==GetConsoleCP() && output_cp==GetConsoleOutputCP();
    printf("WinCon initialization/render/restore: %s (%dx%d)\n",ok ? "PASS" : "FAIL",cols,rows);
    return ok ? 0 : 1;
}
