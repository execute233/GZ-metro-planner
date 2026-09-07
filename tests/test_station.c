#include <stdio.h>
#include <string.h>
#include "../src/adt/station.h"

static int failures = 0;

#define CHECK(cond) do {                                                      \
    if (!(cond)) {                                                            \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
        failures++;                                                           \
    }                                                                         \
} while (0)

static void test_add_and_find_by_id(void) {
    StationTable t;
    station_table_init(&t);

    Station s1 = {0, "体育西路", "tiyuxilu", "Tiyu Xilu"};
    Station s2 = {0, "公园前", "gongyuanqian", "Gongyuanqian"};
    CHECK(station_add(&t, &s1) == 0);
    CHECK(station_add(&t, &s2) == 0);
    CHECK(t.rows.size == 2);
    CHECK(s1.id == 1);   /* 自动分配 id 从 1 开始 */
    CHECK(s2.id == 2);

    Station *found = station_find_by_id(&t, 2);
    CHECK(found != NULL);
    CHECK(strcmp(found->name, "公园前") == 0);
    CHECK(strcmp(found->pinyin_name, "gongyuanqian") == 0);
    CHECK(station_find_by_id(&t, 99) == NULL);

    station_table_dispose(&t);
}

static void test_find_by_name(void) {
    StationTable t;
    station_table_init(&t);
    Station s1 = {0, "体育西路", "tiyuxilu", "Tiyu Xilu"};
    station_add(&t, &s1);

    CHECK(station_find_by_name(&t, "体育西路") != NULL);
    CHECK(station_find_by_name(&t, "不存在的站") == NULL);

    station_table_dispose(&t);
}

static void test_remove_and_next_id(void) {
    StationTable t;
    station_table_init(&t);
    Station s1 = {0, "站A", "zhana", "Station A"};
    Station s2 = {0, "站B", "zhanb", "Station B"};
    Station s3 = {0, "站C", "zhanc", "Station C"};
    station_add(&t, &s1);
    station_add(&t, &s2);
    station_add(&t, &s3);

    CHECK(station_remove(&t, 2) == 0);
    CHECK(t.rows.size == 2);
    CHECK(station_find_by_id(&t, 2) == NULL);
    CHECK(station_remove(&t, 2) == -1);

    Station s4 = {0, "站D", "zhand", "Station D"};
    station_add(&t, &s4);
    CHECK(s4.id == 4);   /* 删除不重用：最大 id 3 + 1 */

    station_table_dispose(&t);
}

int main(void) {
    test_add_and_find_by_id();
    test_find_by_name();
    test_remove_and_next_id();
    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all station checks passed\n");
    return 0;
}