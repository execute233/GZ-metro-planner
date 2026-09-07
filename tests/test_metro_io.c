#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "../src/metro.h"
#include "../src/io/metro_io.h"

static int failures = 0;

#define CHECK(cond) do {                                                      \
    if (!(cond)) {                                                            \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
        failures++;                                                           \
    }                                                                         \
} while (0)

#define TEST_DIR "metro_io_test"

static void write_file(const char *path, const char *content) {
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        fprintf(stderr, "cannot write %s\n", path);
        failures++;
        return;
    }
    fputs(content, f);
    fclose(f);
}

static const char *STATIONS_VALID =
    "id,name,pinyin_name,en_name\n"
    "1,体育西路,tiyuxilu,Tiyu Xilu\n"
    "2,公园前,gongyuanqian,Gongyuanqian\n"
    "3,嘉禾望岗,jiahewanggang,Jiahewanggang\n"
    "4,广州南站,guangzhounanzhan,Guangzhou South Railway Station\n";

static const char *LINES_VALID =
    "id,name,en_name,color,station_ids\n"
    "1,1号线,Line 1,31,1;2\n"
    "2,2号线,Line 2,34,3;2;4\n";

static const char *EDGES_VALID =
    "id,line_id,from_station_id,to_station_id,cost_time_second,cost_meters\n"
    "1,1,1,2,180,4200\n"
    "2,2,3,2,300,6100\n"
    "3,2,2,4,240,5300\n";

static void init_metro(Metro *m) {
    station_table_init(&m->stations);
    line_table_init(&m->lines);
    edge_table_init(&m->edges);
}

static void dispose_metro(Metro *m) {
    station_table_dispose(&m->stations);
    line_table_dispose(&m->lines);
    edge_table_dispose(&m->edges);
}

static void test_load_valid(void) {
    write_file(TEST_DIR "/stations.csv", STATIONS_VALID);
    write_file(TEST_DIR "/lines.csv", LINES_VALID);
    write_file(TEST_DIR "/edges.csv", EDGES_VALID);

    Metro m;
    init_metro(&m);
    CHECK(metro_io_load(TEST_DIR, &m) == 0);
    CHECK(m.stations.rows.size == 4);
    CHECK(m.lines.rows.size == 2);
    CHECK(m.edges.rows.size == 3);

    Station *st = station_find_by_name(&m.stations, "体育西路");
    CHECK(st != NULL && st->id == 1);
    CHECK(strcmp(st->pinyin_name, "tiyuxilu") == 0);

    Line *l2 = line_find_by_name(&m.lines, "2号线");
    CHECK(l2 != NULL && l2->station_ids.size == 3);
    CHECK(l2->station_ids.items[0] == 3);
    CHECK(l2->station_ids.items[1] == 2);
    CHECK(l2->station_ids.items[2] == 4);

    Edge *e = edge_find_by_id(&m.edges, 3);
    CHECK(e != NULL && e->cost_time_second == 240 && e->cost_meters == 5300);

    dispose_metro(&m);
}

static void test_load_bom(void) {
    /* 首行带 UTF-8 BOM，应被剥离 */
    write_file(TEST_DIR "/stations.csv", STATIONS_VALID);
    FILE *f = fopen(TEST_DIR "/stations.csv", "r+b");
    if (f != NULL) {
        fwrite("\xEF\xBB\xBF", 1, 3, f);
        fclose(f);
    }
    write_file(TEST_DIR "/lines.csv", LINES_VALID);
    write_file(TEST_DIR "/edges.csv", EDGES_VALID);

    Metro m;
    init_metro(&m);
    CHECK(metro_io_load(TEST_DIR, &m) == 0);
    CHECK(station_find_by_id(&m.stations, 1) != NULL);
    dispose_metro(&m);
}

static void test_load_missing_file(void) {
    Metro m;
    init_metro(&m);
    CHECK(metro_io_load("no_such_dir", &m) == -1);
    dispose_metro(&m);
}

static void test_validate_failures(void) {
    /* 边引用不存在的站 */
    write_file(TEST_DIR "/stations.csv", STATIONS_VALID);
    write_file(TEST_DIR "/lines.csv", LINES_VALID);
    write_file(TEST_DIR "/edges.csv",
        "id,line_id,from_station_id,to_station_id,cost_time_second,cost_meters\n"
        "1,1,1,99,180,4200\n");
    Metro m;
    init_metro(&m);
    char err[256];
    CHECK(metro_io_load(TEST_DIR, &m) == -1);
    CHECK(metro_io_validate(&m, err, sizeof(err)) == -1);
    dispose_metro(&m);

    /* 线路站序引用不存在的站 */
    write_file(TEST_DIR "/lines.csv",
        "id,name,en_name,color,station_ids\n"
        "1,1号线,Line 1,31,1;99\n");
    write_file(TEST_DIR "/edges.csv",
        "id,line_id,from_station_id,to_station_id,cost_time_second,cost_meters\n"
        "1,1,1,99,180,4200\n");
    init_metro(&m);
    CHECK(metro_io_load(TEST_DIR, &m) == -1);
    dispose_metro(&m);

    /* 边两端不是该线相邻站 */
    write_file(TEST_DIR "/lines.csv", LINES_VALID);
    write_file(TEST_DIR "/edges.csv",
        "id,line_id,from_station_id,to_station_id,cost_time_second,cost_meters\n"
        "1,1,1,2,180,4200\n"
        "2,2,3,4,300,6100\n");
    init_metro(&m);
    CHECK(metro_io_load(TEST_DIR, &m) == -1);
    dispose_metro(&m);

    /* cost_meters 为 0 */
    write_file(TEST_DIR "/edges.csv",
        "id,line_id,from_station_id,to_station_id,cost_time_second,cost_meters\n"
        "1,1,1,2,180,0\n");
    init_metro(&m);
    CHECK(metro_io_load(TEST_DIR, &m) == -1);
    dispose_metro(&m);

    /* 同线 from/to 互换重复边 */
    write_file(TEST_DIR "/edges.csv",
        "id,line_id,from_station_id,to_station_id,cost_time_second,cost_meters\n"
        "1,1,1,2,180,4200\n"
        "2,1,2,1,200,4400\n");
    init_metro(&m);
    CHECK(metro_io_load(TEST_DIR, &m) == -1);
    dispose_metro(&m);

    /* 站 id 重复 */
    write_file(TEST_DIR "/stations.csv",
        "id,name,pinyin_name,en_name\n"
        "1,体育西路,tiyuxilu,Tiyu Xilu\n"
        "1,公园前,gongyuanqian,Gongyuanqian\n");
    write_file(TEST_DIR "/lines.csv",
        "id,name,en_name,color,station_ids\n"
        "1,1号线,Line 1,31,1;2\n");
    write_file(TEST_DIR "/edges.csv",
        "id,line_id,from_station_id,to_station_id,cost_time_second,cost_meters\n"
        "1,1,1,2,180,4200\n");
    init_metro(&m);
    CHECK(metro_io_load(TEST_DIR, &m) == -1);
    dispose_metro(&m);

    /* 负数站 id（会导致 graph_build 越界写） */
    write_file(TEST_DIR "/stations.csv",
        "id,name,pinyin_name,en_name\n"
        "-1,体育西路,tiyuxilu,Tiyu Xilu\n"
        "2,公园前,gongyuanqian,Gongyuanqian\n");
    write_file(TEST_DIR "/lines.csv",
        "id,name,en_name,color,station_ids\n"
        "1,1号线,Line 1,31,-1;2\n");
    write_file(TEST_DIR "/edges.csv",
        "id,line_id,from_station_id,to_station_id,cost_time_second,cost_meters\n"
        "1,1,-1,2,180,4200\n");
    init_metro(&m);
    CHECK(metro_io_load(TEST_DIR, &m) == -1);
    dispose_metro(&m);
}

static void test_save_roundtrip(void) {
    write_file(TEST_DIR "/stations.csv", STATIONS_VALID);
    write_file(TEST_DIR "/lines.csv", LINES_VALID);
    write_file(TEST_DIR "/edges.csv", EDGES_VALID);

    Metro m1, m2;
    init_metro(&m1);
    CHECK(metro_io_load(TEST_DIR, &m1) == 0);

    /* 保存到另一个目录再载入，数据一致 */
    remove(TEST_DIR "/save/stations.csv");
    remove(TEST_DIR "/save/lines.csv");
    remove(TEST_DIR "/save/edges.csv");
    remove(TEST_DIR "/save");
    mkdir(TEST_DIR "/save");
    CHECK(metro_io_save(TEST_DIR "/save", &m1) == 0);

    init_metro(&m2);
    CHECK(metro_io_load(TEST_DIR "/save", &m2) == 0);
    CHECK(m2.stations.rows.size == m1.stations.rows.size);
    CHECK(m2.lines.rows.size == m1.lines.rows.size);
    CHECK(m2.edges.rows.size == m1.edges.rows.size);

    Station *st = station_find_by_name(&m2.stations, "嘉禾望岗");
    CHECK(st != NULL && st->id == 3);
    Line *l1 = line_find_by_name(&m2.lines, "1号线");
    CHECK(l1 != NULL && l1->station_ids.size == 2);
    Edge *e = edge_find_by_id(&m2.edges, 2);
    CHECK(e != NULL && e->line_id == 2 && e->cost_meters == 6100);

    dispose_metro(&m1);
    dispose_metro(&m2);
}

int main(void) {
    mkdir(TEST_DIR);
    test_load_valid();
    test_load_bom();
    test_load_missing_file();
    test_validate_failures();
    test_save_roundtrip();

    remove(TEST_DIR "/stations.csv");
    remove(TEST_DIR "/lines.csv");
    remove(TEST_DIR "/edges.csv");
    remove(TEST_DIR "/save/stations.csv");
    remove(TEST_DIR "/save/lines.csv");
    remove(TEST_DIR "/save/edges.csv");
    remove(TEST_DIR "/save");
    remove(TEST_DIR);

    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all metro_io checks passed\n");
    return 0;
}