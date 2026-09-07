#include <stdio.h>
#include "../src/adt/edge.h"

static int failures = 0;

#define CHECK(cond) do {                                                      \
    if (!(cond)) {                                                            \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
        failures++;                                                           \
    }                                                                         \
} while (0)

static void test_add_and_find_by_id(void) {
    EdgeTable t;
    edge_table_init(&t);

    Edge e1 = {0, 1, 1, 2, 180, 4200};
    Edge e2 = {0, 1, 2, 3, 240, 5300};
    CHECK(edge_add(&t, &e1) == 0);
    CHECK(edge_add(&t, &e2) == 0);
    CHECK(e1.id == 1);
    CHECK(e2.id == 2);

    Edge *found = edge_find_by_id(&t, 2);
    CHECK(found != NULL);
    CHECK(found->line_id == 1);
    CHECK(found->from_station_id == 2);
    CHECK(found->to_station_id == 3);
    CHECK(found->cost_time_second == 240);
    CHECK(found->cost_meters == 5300);
    CHECK(edge_find_by_id(&t, 99) == NULL);

    edge_table_dispose(&t);
}

static void test_find_by_station(void) {
    EdgeTable t;
    edge_table_init(&t);

    Edge e1 = {0, 1, 1, 2, 180, 4200};
    Edge e2 = {0, 1, 2, 3, 240, 5300};
    Edge e3 = {0, 2, 4, 2, 300, 6100};
    edge_add(&t, &e1);
    edge_add(&t, &e2);
    edge_add(&t, &e3);

    ArrayList_Int out;
    al_int_init(&out);

    /* 站 2 作为起点：只有 e2 */
    int n = edge_find_by_from_station(&t, 2, &out);
    CHECK(n == 1);
    CHECK(out.size == 1);
    CHECK(out.items[0] == 2);

    /* 站 2 作为终点：e1 和 e3 */
    n = edge_find_by_to_station(&t, 2, &out);
    CHECK(n == 2);
    CHECK(out.size == 3);
    CHECK(out.items[1] == 1);
    CHECK(out.items[2] == 3);

    /* 不存在 */
    CHECK(edge_find_by_from_station(&t, 999, &out) == 0);

    al_int_dispose(&out);
    edge_table_dispose(&t);
}

static void test_find_between(void) {
    EdgeTable t;
    edge_table_init(&t);

    Edge e1 = {0, 1, 1, 2, 180, 4200};
    Edge e2 = {0, 1, 2, 3, 240, 5300};
    Edge e3 = {0, 2, 3, 2, 120, 2600};   /* 共线段：不同线同两端 */
    edge_add(&t, &e1);
    edge_add(&t, &e2);
    edge_add(&t, &e3);

    ArrayList_Int out;
    al_int_init(&out);

    /* 正方向命中 */
    CHECK(edge_find_between(&t, 1, 2, &out) == 1);
    CHECK(out.items[0] == 1);
    /* 反方向也命中（无向） */
    CHECK(edge_find_between(&t, 2, 1, &out) == 1);
    CHECK(out.size == 2);
    /* 共线段两条都返回 */
    CHECK(edge_find_between(&t, 2, 3, &out) == 2);
    CHECK(out.size == 4);
    CHECK(out.items[2] == 2);
    CHECK(out.items[3] == 3);
    /* 不相连 */
    CHECK(edge_find_between(&t, 1, 4, &out) == 0);

    al_int_dispose(&out);
    edge_table_dispose(&t);
}

static void test_remove_and_next_id(void) {
    EdgeTable t;
    edge_table_init(&t);

    Edge e1 = {0, 1, 1, 2, 180, 4200};
    Edge e2 = {0, 1, 2, 3, 240, 5300};
    edge_add(&t, &e1);
    edge_add(&t, &e2);

    CHECK(edge_remove(&t, 1) == 0);
    CHECK(t.rows.size == 1);
    CHECK(edge_find_by_id(&t, 1) == NULL);
    CHECK(edge_remove(&t, 1) == -1);

    Edge e3 = {0, 2, 5, 6, 200, 4800};
    edge_add(&t, &e3);
    CHECK(e3.id == 3);   /* 删除不重用 */

    edge_table_dispose(&t);
}

int main(void) {
    test_add_and_find_by_id();
    test_find_by_station();
    test_find_between();
    test_remove_and_next_id();
    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all edge checks passed\n");
    return 0;
}