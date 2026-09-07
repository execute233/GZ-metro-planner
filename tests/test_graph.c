#include <stdio.h>
#include "../src/metro.h"
#include "../src/algo/graph.h"

static int failures = 0;

#define CHECK(cond) do {                                                      \
    if (!(cond)) {                                                            \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
        failures++;                                                           \
    }                                                                         \
} while (0)

static int has_neighbor(const ArrayList_Int *list, int id) {
    for (size_t i = 0; i < list->size; i++) {
        if (list->items[i] == id)
            return 1;
    }
    return 0;
}

static Metro build_metro(void) {
    Metro m;
    station_table_init(&m.stations);
    line_table_init(&m.lines);
    edge_table_init(&m.edges);

    Station s1 = {0, "体育西路", "tiyuxilu", "Tiyu Xilu"};
    Station s2 = {0, "公园前", "gongyuanqian", "Gongyuanqian"};
    Station s3 = {0, "嘉禾望岗", "jiahewanggang", "Jiahewanggang"};
    Station s4 = {0, "广州南站", "guangzhounanzhan", "Guangzhou South Railway Station"};
    station_add(&m.stations, &s1);
    station_add(&m.stations, &s2);
    station_add(&m.stations, &s3);
    station_add(&m.stations, &s4);

    Line l1 = {0, "1号线", "Line 1", 31, {0}};
    al_int_init(&l1.station_ids);
    al_int_push(&l1.station_ids, 1);
    al_int_push(&l1.station_ids, 2);
    line_add(&m.lines, &l1);
    al_int_dispose(&l1.station_ids);

    Line l2 = {0, "2号线", "Line 2", 34, {0}};
    al_int_init(&l2.station_ids);
    al_int_push(&l2.station_ids, 3);
    al_int_push(&l2.station_ids, 2);
    al_int_push(&l2.station_ids, 4);
    line_add(&m.lines, &l2);
    al_int_dispose(&l2.station_ids);

    Edge e1 = {0, 1, 1, 2, 180, 4200};
    Edge e2 = {0, 2, 3, 2, 300, 6100};
    Edge e3 = {0, 2, 2, 4, 240, 5300};
    edge_add(&m.edges, &e1);
    edge_add(&m.edges, &e2);
    edge_add(&m.edges, &e3);

    return m;
}

static void test_build_and_neighbors(void) {
    Metro m = build_metro();
    Graph g;
    CHECK(graph_build(&g, &m) == 0);
    CHECK(g.capacity == 5);   /* 最大站 id 4 + 1 */

    const ArrayList_Int *n1 = graph_neighbors(&g, 1);
    CHECK(n1 != NULL && n1->size == 1);
    CHECK(has_neighbor(n1, 2));

    const ArrayList_Int *n2 = graph_neighbors(&g, 2);
    CHECK(n2 != NULL && n2->size == 3);
    CHECK(has_neighbor(n2, 1));
    CHECK(has_neighbor(n2, 3));
    CHECK(has_neighbor(n2, 4));

    const ArrayList_Int *n3 = graph_neighbors(&g, 3);
    CHECK(n3 != NULL && n3->size == 1);
    CHECK(has_neighbor(n3, 2));

    const ArrayList_Int *n4 = graph_neighbors(&g, 4);
    CHECK(n4 != NULL && n4->size == 1);
    CHECK(has_neighbor(n4, 2));

    /* 空洞节点（id 0 无站）返回空列表而非 NULL */
    const ArrayList_Int *n0 = graph_neighbors(&g, 0);
    CHECK(n0 != NULL && n0->size == 0);

    /* 越界返回 NULL */
    CHECK(graph_neighbors(&g, 999) == NULL);

    graph_dispose(&g);

    station_table_dispose(&m.stations);
    line_table_dispose(&m.lines);
    edge_table_dispose(&m.edges);
}

int main(void) {
    test_build_and_neighbors();
    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all graph checks passed\n");
    return 0;
}