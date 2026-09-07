#include <stdio.h>
#include "../src/metro.h"
#include "../src/algo/graph.h"
#include "../src/algo/router.h"

static int failures = 0;

#define CHECK(cond) do {                                                      \
    if (!(cond)) {                                                            \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
        failures++;                                                           \
    }                                                                         \
} while (0)

/*
 * 测试网络：
 *  1号线: 1(体育西路) - 2(公园前)
 *  2号线: 3(嘉禾望岗) - 2(公园前) - 4(广州南站)
 *  3号线: 5(杨箕)    - 4(广州南站)
 *  4号线: 1(体育西路) - 5(杨箕)     [边 1-5：1000m / 300s]
 *  6(员村) 孤立站
 * 换乘站：公园前(1/2)、广州南站(2/3)、体育西路(1/4)
 */
static Metro build_metro(void) {
    Metro m;
    station_table_init(&m.stations);
    line_table_init(&m.lines);
    edge_table_init(&m.edges);

    static const char *names[] = {"", "体育西路", "公园前", "嘉禾望岗",
                                  "广州南站", "杨箕", "员村"};
    for (int id = 1; id <= 6; id++) {
        Station s;
        s.id = 0;
        snprintf(s.name, sizeof(s.name), "%s", names[id]);
        snprintf(s.pinyin_name, sizeof(s.pinyin_name), "p%d", id);
        snprintf(s.en_name, sizeof(s.en_name), "S%d", id);
        station_add(&m.stations, &s);
    }

    static const int line_stations[4][4] = {
        {1, 2, 0, 0},
        {3, 2, 4, 0},
        {5, 4, 0, 0},
        {1, 5, 0, 0},
    };
    static const char *line_names[] = {"1号线", "2号线", "3号线", "4号线"};
    for (int li = 0; li < 4; li++) {
        Line ln;
        memset(&ln, 0, sizeof(ln));
        snprintf(ln.name, sizeof(ln.name), "%s", line_names[li]);
        ln.color = 31 + li;
        al_int_init(&ln.station_ids);
        for (int k = 0; line_stations[li][k] != 0; k++)
            al_int_push(&ln.station_ids, line_stations[li][k]);
        line_add(&m.lines, &ln);
        al_int_dispose(&ln.station_ids);
    }

    /* 1-2 4200m/180s, 3-2 6100m/300s, 2-4 5300m/240s, 5-4 3300m/150s, 1-5 1000m/300s */
    static const int edge_data[5][6] = {
        {1, 1, 1, 2, 180, 4200},
        {2, 2, 3, 2, 300, 6100},
        {3, 2, 2, 4, 240, 5300},
        {4, 3, 5, 4, 150, 3300},
        {5, 4, 1, 5, 300, 1000},
    };
    for (int i = 0; i < 5; i++) {
        Edge e;
        e.id = 0;
        e.line_id = edge_data[i][1];
        e.from_station_id = edge_data[i][2];
        e.to_station_id = edge_data[i][3];
        e.cost_time_second = edge_data[i][4];
        e.cost_meters = edge_data[i][5];
        edge_add(&m.edges, &e);
    }
    return m;
}

static void test_min_stations_with_transfer(void) {
    Metro m = build_metro();
    Graph g;
    graph_build(&g, &m);

    Route r;
    route_init(&r);
    /* 嘉禾望岗(3) → 体育西路(1)：唯一路径 3→2→1，在公园前(2)换乘 */
    CHECK(router_find_route(&g, &m, 3, 1, ROUTE_MIN_STATIONS, &r) == 0);
    CHECK(r.total_stations == 3);
    CHECK(r.stations.size == 3);
    CHECK(r.stations.items[0] == 3);
    CHECK(r.stations.items[1] == 2);
    CHECK(r.stations.items[2] == 1);
    CHECK(r.edges_ids.size == 2);
    CHECK(r.edges_ids.items[0] == 2);
    CHECK(r.edges_ids.items[1] == 1);
    CHECK(r.transfers.size == 1);
    CHECK(r.transfers.items[0] == 2);
    CHECK(r.total_meters == 6100 + 4200);
    CHECK(r.total_seconds == 300 + 180);

    route_dispose(&r);
    graph_dispose(&g);
    station_table_dispose(&m.stations);
    line_table_dispose(&m.lines);
    edge_table_dispose(&m.edges);
}

static void test_min_distance_vs_min_time(void) {
    Metro m = build_metro();
    Graph g;
    graph_build(&g, &m);

    /* 嘉禾望岗(3) → 杨箕(5)：
     * 路径A 3→2→4→5: 14700m / 690s
     * 路径B 3→2→1→5: 11300m / 780s
     * 最少站点：两条都是 4 站
     */
    Route r;
    route_init(&r);
    CHECK(router_find_route(&g, &m, 3, 5, ROUTE_MIN_DISTANCE, &r) == 0);
    CHECK(r.total_stations == 4);
    CHECK(r.total_meters == 11300);
    CHECK(r.total_seconds == 780);
    CHECK(r.stations.items[0] == 3);
    CHECK(r.stations.items[3] == 5);
    CHECK(r.transfers.size == 2);   /* 公园前 + 体育西路 */
    route_dispose(&r);

    route_init(&r);
    CHECK(router_find_route(&g, &m, 3, 5, ROUTE_MIN_TIME, &r) == 0);
    CHECK(r.total_stations == 4);
    CHECK(r.total_meters == 14700);
    CHECK(r.total_seconds == 690);
    CHECK(r.stations.items[0] == 3);
    CHECK(r.stations.items[3] == 5);
    CHECK(r.transfers.size == 1);   /* 广州南站 */
    route_dispose(&r);

    graph_dispose(&g);
    station_table_dispose(&m.stations);
    line_table_dispose(&m.lines);
    edge_table_dispose(&m.edges);
}

static void test_unreachable_and_missing(void) {
    Metro m = build_metro();
    Graph g;
    graph_build(&g, &m);

    Route r;
    route_init(&r);
    /* 孤立站 6(员村) 不可达 */
    CHECK(router_find_route(&g, &m, 1, 6, ROUTE_MIN_STATIONS, &r) == -1);
    /* 不存在的站 */
    CHECK(router_find_route(&g, &m, 1, 999, ROUTE_MIN_STATIONS, &r) == -1);
    CHECK(router_find_route(&g, &m, 999, 1, ROUTE_MIN_STATIONS, &r) == -1);

    /* 起终点相同 */
    CHECK(router_find_route(&g, &m, 2, 2, ROUTE_MIN_STATIONS, &r) == 0);
    CHECK(r.total_stations == 1);
    CHECK(r.stations.size == 1);
    CHECK(r.stations.items[0] == 2);
    CHECK(r.edges_ids.size == 0);
    CHECK(r.total_meters == 0);

    route_dispose(&r);
    graph_dispose(&g);
    station_table_dispose(&m.stations);
    line_table_dispose(&m.lines);
    edge_table_dispose(&m.edges);
}

static void test_shared_segment(void) {
    /* 共线段：1-2 同时属于 1号线(4200m/180s) 与 2号线(2600m/120s)，
     * 2号线还有 2-3(1000m/60s)。1→3 最短路程应取 2号线共线段边 */
    Metro m;
    station_table_init(&m.stations);
    line_table_init(&m.lines);
    edge_table_init(&m.edges);

    Station s1 = {0, "站A", "zhana", "A"};
    Station s2 = {0, "站B", "zhanb", "B"};
    Station s3 = {0, "站C", "zhanc", "C"};
    station_add(&m.stations, &s1);
    station_add(&m.stations, &s2);
    station_add(&m.stations, &s3);

    Line l1 = {0, "1号线", "Line 1", 31, {0}};
    al_int_init(&l1.station_ids);
    al_int_push(&l1.station_ids, 1);
    al_int_push(&l1.station_ids, 2);
    line_add(&m.lines, &l1);
    al_int_dispose(&l1.station_ids);

    Line l2 = {0, "2号线", "Line 2", 34, {0}};
    al_int_init(&l2.station_ids);
    al_int_push(&l2.station_ids, 1);
    al_int_push(&l2.station_ids, 2);
    al_int_push(&l2.station_ids, 3);
    line_add(&m.lines, &l2);
    al_int_dispose(&l2.station_ids);

    Edge e1 = {0, 1, 1, 2, 180, 4200};
    Edge e2 = {0, 2, 1, 2, 120, 2600};
    Edge e3 = {0, 2, 2, 3, 60, 1000};
    edge_add(&m.edges, &e1);
    edge_add(&m.edges, &e2);
    edge_add(&m.edges, &e3);

    Graph g;
    graph_build(&g, &m);
    Route r;
    route_init(&r);

    /* 最短路程：走 2号线 2600+1000=3600，无换乘 */
    CHECK(router_find_route(&g, &m, 1, 3, ROUTE_MIN_DISTANCE, &r) == 0);
    CHECK(r.total_meters == 3600);
    CHECK(r.total_seconds == 180);
    CHECK(r.transfers.size == 0);
    CHECK(r.edges_ids.items[0] == 2);   /* 取的是 2号线共线段边 */
    CHECK(r.edges_ids.items[1] == 3);
    route_dispose(&r);

    /* 最少时间：同路径 120+60=180s */
    route_init(&r);
    CHECK(router_find_route(&g, &m, 1, 3, ROUTE_MIN_TIME, &r) == 0);
    CHECK(r.total_seconds == 180);
    CHECK(r.total_meters == 3600);
    route_dispose(&r);

    route_init(&r);
    CHECK(router_find_route(&g, &m, 1, 3, ROUTE_MIN_STATIONS, &r) == 0);
    CHECK(r.total_stations == 3);
    route_dispose(&r);

    graph_dispose(&g);
    station_table_dispose(&m.stations);
    line_table_dispose(&m.lines);
    edge_table_dispose(&m.edges);
}

int main(void) {
    test_min_stations_with_transfer();
    test_min_distance_vs_min_time();
    test_unreachable_and_missing();
    test_shared_segment();
    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all router checks passed\n");
    return 0;
}