#include "../src/ui/tui_state.h"
#include "../src/ui/tui_maintain.h"
#include "../src/io/metro_io.h"
#include "../src/render/utf8.h"
#include "../src/render/render.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <windows.h>
#include <direct.h>
static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)
static TuiEvent key(TuiKey k) { return (TuiEvent){.type = EVENT_KEY, .key = k}; }
static TuiEvent letter(uint32_t ch) { return (TuiEvent){.type = EVENT_TEXT, .text = ch}; }
static void type(TuiState *s, const char *str) { while (*str) tui_state_event(s, letter(utf8_decode(&str))); }
static void test_unicode(void) {
    CHECK(render_display_width("广州⣿─A") == 7);
    char text[12] = "广州"; size_t cursor = strlen(text);
    utf8_backspace(text, &cursor); CHECK(!strcmp(text, "广") && cursor == 3);
    CHECK(!utf8_insert(text, sizeof(text), &cursor, 0x5dde)); CHECK(!strcmp(text, "广州"));
    tui_edit(text, sizeof(text), &cursor, key(TKEY_LEFT));
    CHECK(!utf8_insert(text, sizeof(text), &cursor, 'A')); CHECK(!strcmp(text, "广A州"));
    CHECK(utf8_insert(text, 8, &cursor, 0x4e2d) == -1); CHECK(!strcmp(text, "广A州"));
    const char *bad = "\xe0\x80\x80"; CHECK(utf8_decode(&bad) == 0xfffd);
    CellSurface out = {0}; CHECK(!surface_resize(&out, 4, 2)); surface_clear(&out);
    surface_text(&out, 0, 0, 3, "广州", 0xffffff); CHECK(out.cells[1].continuation && out.cells[2].cp == ' ');
    surface_put(&out, 1, 0, 'x', 1); CHECK(out.cells[0].cp == ' ' && !out.cells[1].continuation);
    surface_put(&out, 3, 1, 0x5dde, 1); CHECK(out.cells[7].cp == ' ');
    surface_dispose(&out);
}
static void test_canvas(void) {
    BrailleCanvas b = {0}; CHECK(!braille_resize(&b, 10, 10)); braille_clear(&b);
    const int bits[4][2] = {{1,8},{2,16},{4,32},{64,128}};
    for (int y = 0; y < 4; y++) for (int x = 0; x < 2; x++) {
        braille_clear(&b); braille_pixel(&b, x, y, 1, 0); CHECK(b.cells[0].mask == bits[y][x]);
    }
    braille_pixel(&b, 0, 0, 2, 10); braille_pixel(&b, 1, 1, 3, 1); CHECK(b.cells[0].rgb == 2);
    const int endpoints[8][2] = {{19,20},{19,39},{10,39},{0,39},{0,20},{0,0},{10,0},{19,0}};
    for (int i = 0; i < 8; i++) {
        braille_clear(&b); braille_line(&b, 10,20,endpoints[i][0],endpoints[i][1],1,0);
        CHECK(b.cells[5*10+5].mask != 0); CHECK(b.cells[(endpoints[i][1]/4)*10+endpoints[i][0]/2].mask != 0);
    }
    braille_clear(&b); braille_line(&b, -1e9, 4, 1e9, 4, 1, 0); CHECK(b.cells[10].mask && b.cells[19].mask);
    braille_line(&b, NAN,0,1,1,1,0); braille_dispose(&b);
    Viewport v = {.cols=80,.rows=30}; viewport_fit(&v, (MapPoint){0,0}, (MapPoint){100,200}, 1);
    MapPoint p = {34,78}, q = viewport_unproject(&v, viewport_project(&v,p));
    CHECK(fabs(p.x-q.x)<1e-9 && fabs(p.y-q.y)<1e-9);
    MapPoint anchor = {50,30}, before = viewport_unproject(&v,anchor);
    viewport_zoom_at(&v, 2, anchor); q = viewport_unproject(&v,anchor);
    CHECK(fabs(before.x-q.x)<1e-9 && fabs(before.y-q.y)<1e-9);
}
static void export_frame(const char *file, const CellSurface *out) {
    FILE *f = fopen(file, "wb"); CHECK(f != NULL); if (!f) return;
    fprintf(f, "%d %d\n", out->cols, out->rows);
    for (int i = 0; i < out->cols*out->rows; i++) fprintf(f, "%u %u %d\n", out->cells[i].cp, out->cells[i].rgb, out->cells[i].continuation);
    fclose(f);
}
static void test_map_and_state(void) {
    TuiState s; CHECK(!tui_state_init(&s, GZMP_TEST_DATA)); CHECK(s.layout_valid);
    CHECK(s.map.stations.size == 56 && s.map.vertices.size == 114);
    int count; CHECK(tui_search_station(&s.metro, "TIYUX", 0, &count) == 14 && count == 1);
    tui_state_resize(&s,120,40);
    double zoom = s.view.zoom; tui_state_event(&s, letter('+')); CHECK(s.view.zoom > zoom);
    tui_state_event(&s, key(TKEY_TAB)); type(&s,"guangzhounan"); tui_state_event(&s,key(TKEY_ENTER)); CHECK(s.from == 35);
    s.focus = 2; s.search[0] = 0; s.cursor = 0; type(&s,"tiyux"); tui_state_event(&s,key(TKEY_ENTER)); CHECK(s.to == 14);
    s.focus = 4; tui_state_event(&s,key(TKEY_ENTER)); CHECK(s.has_route);
    CHECK(s.route.stations.items[0] == 35 && s.route.stations.items[s.route.stations.size-1] == 14);
    CellSurface out = {0}; BrailleCanvas b = {0};
    CHECK(!tui_state_render(&s,&out,&b)); export_frame("tui-route-120.frame",&out);
    tui_state_resize(&s,50,16); s.focus=7;
    for (int i=0;i<100;i++) tui_state_event(&s,key(TKEY_DOWN));
    CHECK(s.result_scroll == (int)s.route.edges_ids.size+(int)s.route.transfers.size+1-3);
    CHECK(!tui_state_render(&s,&out,&b));
    bool last_station=false;
    for (int y=11;y<14;y++) for (int x=0;x<out.cols;x++) if (out.cells[y*out.cols+x].cp==0x4f53) last_station=true;
    CHECK(last_station);
    s.focus=2; strcpy(s.search,"体育西路"); s.cursor=strlen(s.search); s.candidate=0;
    CHECK(!tui_state_render(&s,&out,&b)); CHECK(out.cells[9*out.cols+2].cp=='>');
    tui_state_resize(&s,120,40); s.focus=4;
    s.has_route = false; tui_fit(&s,false); CHECK(!tui_state_render(&s,&out,&b)); export_frame("tui-map-120.frame",&out);
    const int sizes[][2] = {{90,30},{70,24},{50,16},{40,10}};
    for (int i = 0; i < 4; i++) {
        tui_state_resize(&s,sizes[i][0],sizes[i][1]); CHECK(!tui_state_render(&s,&out,&b));
        for (int j = 0; j < out.cols*out.rows; j++) if (out.cells[j].continuation) CHECK(j%out.cols > 0 && utf8_width(out.cells[j-1].cp) == 2);
    }
    tui_state_resize(&s,70,24); tui_fit(&s,false); s.focus=0;
    CHECK(!tui_state_render(&s,&out,&b)); export_frame("tui-map-70.frame",&out);
    s.focus=1; CHECK(!tui_state_render(&s,&out,&b)); export_frame("tui-side-70.frame",&out);
    tui_state_resize(&s,120,40); s.page=PAGE_MAINTAIN; CHECK(!tui_state_render(&s,&out,&b)); export_frame("tui-maintain.frame",&out);
    size_t n=s.metro.stations.rows.size; tui_state_event(&s,key(TKEY_ENTER)); type(&s,"取消测试"); tui_state_event(&s,key(TKEY_ESCAPE)); CHECK(s.metro.stations.rows.size==n && s.page==PAGE_MAP);
    char error[256]; s.map.vertices.items[0].point.x += 1; CHECK(map_io_validate(&s.metro,&s.map,error,sizeof(error)) == -1 && strstr(error,"map_segments.csv:2"));
    s.map.vertices.items[0].point.x -= 1;
    s.map.stations.items[0].label_dx=INT_MIN; CHECK(map_io_validate(&s.metro,&s.map,error,sizeof(error)) == -1);
    surface_dispose(&out); braille_dispose(&b); tui_state_dispose(&s);
}
static const char *names[] = {"stations.csv","lines.csv","edges.csv","map_stations.csv","map_segments.csv","map_styles.csv"};
static void test_persistence(void) {
    char dir[128]; snprintf(dir,sizeof(dir),"tui-io-%lu",GetCurrentProcessId()); CHECK(!_mkdir(dir));
    TuiState source; CHECK(!tui_state_init(&source,GZMP_TEST_DATA)); char error[256];
    CHECK(!project_io_save(dir,&source.metro,&source.map,error,sizeof(error)));
    TuiState s; CHECK(!tui_state_init(&s,dir)); tui_state_resize(&s,120,40);
    s.page=PAGE_MAINTAIN; s.menu=0; tui_state_event(&s,key(TKEY_ENTER));
    type(&s,"测试站"); tui_state_event(&s,key(TKEY_ENTER)); type(&s,"ceshi"); tui_state_event(&s,key(TKEY_ENTER)); tui_state_event(&s,key(TKEY_ENTER));
    CHECK(s.page==PAGE_PICK); tui_state_event(&s,key(TKEY_ENTER)); CHECK(s.page==PAGE_CONFIRM); tui_state_event(&s,key(TKEY_ENTER)); CHECK(s.page==PAGE_MAP);
    CHECK(s.metro.stations.rows.size==57 && s.map.stations.size==57);
    /* Create a line via the same events used by the UI. */
    s.page=PAGE_MAINTAIN; s.menu=2; tui_state_event(&s,key(TKEY_ENTER)); type(&s,"测试线"); tui_state_event(&s,key(TKEY_ENTER));
    type(&s,"#00FF00"); tui_state_event(&s,key(TKEY_ENTER)); type(&s,"测试站"); tui_state_event(&s,key(TKEY_ENTER));
    type(&s,"体育西路"); tui_state_event(&s,key(TKEY_ENTER)); tui_state_event(&s,key(TKEY_ENTER)); type(&s,"120,1500"); tui_state_event(&s,key(TKEY_ENTER));
    CHECK(s.page==PAGE_CONFIRM); tui_state_event(&s,key(TKEY_ENTER)); CHECK(s.page==PAGE_MAP && s.metro.lines.rows.size==4);
    s.from=57; s.to=14; s.focus=4; tui_state_event(&s,key(TKEY_ENTER)); CHECK(s.has_route && s.route.total_seconds==120);
    /* A denied replacement must roll back all previously replaced files. */
    char path[256]; snprintf(path,sizeof(path),"%s/edges.csv",dir);
    HANDLE lock=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL); CHECK(lock!=INVALID_HANDLE_VALUE);
    CHECK(project_io_save(dir,&source.metro,&source.map,error,sizeof(error)) == -1);
    CloseHandle(lock);
    CHECK(!project_io_recover(dir,error,sizeof(error)));
    TuiState reload; CHECK(!tui_state_init(&reload,dir)); CHECK(reload.metro.stations.rows.size==57 && reload.metro.lines.rows.size==4); tui_state_dispose(&reload);
    /* Delete referenced station is rejected, with form and data retained. */
    s.page=PAGE_MAINTAIN; s.menu=1; tui_state_event(&s,key(TKEY_ENTER)); type(&s,"测试站"); tui_state_event(&s,key(TKEY_ENTER)); tui_state_event(&s,key(TKEY_ENTER));
    CHECK(s.page==PAGE_CONFIRM && s.metro.stations.rows.size==57);
    tui_state_event(&s,key(TKEY_ESCAPE)); s.page=PAGE_MAINTAIN; s.menu=3; tui_state_event(&s,key(TKEY_ENTER)); type(&s,"测试线"); tui_state_event(&s,key(TKEY_ENTER)); tui_state_event(&s,key(TKEY_ENTER));
    CHECK(s.page==PAGE_MAP && !s.has_route && s.metro.lines.rows.size==3 && s.metro.edges.rows.size==57);
    snprintf(path,sizeof(path),"%s/map_stations.csv",dir);
    FILE *bad=fopen(path,"wb"); CHECK(bad!=NULL);
    if (bad) { fputs("station_id,x,y,label_dx,label_dy,label_anchor\n99999999999999999999999999999,0,0,2,0,right\n",bad); fclose(bad); }
    MapDocument invalid={0}; CHECK(map_io_load(dir,&s.metro,&invalid,error,sizeof(error))==-1); CHECK(strstr(error,"map_stations.csv:2")); map_dispose(&invalid);
    CHECK(!tui_state_init(&reload,dir)); CHECK(!reload.layout_valid); tui_state_resize(&reload,120,40);
    reload.page=PAGE_MAINTAIN; tui_state_event(&reload,key(TKEY_ENTER)); CHECK(reload.page==PAGE_MAINTAIN); tui_state_dispose(&reload);
    tui_state_dispose(&s); tui_state_dispose(&source);
    for (int i=0;i<6;i++) { snprintf(path,sizeof(path),"%s/%s",dir,names[i]); CHECK(!remove(path)); }
    CHECK(!_rmdir(dir));
}
int main(void) {
    test_unicode(); test_canvas(); test_map_and_state(); test_persistence();
    printf("%d failures\n",failures); return failures ? 1 : 0;
}
