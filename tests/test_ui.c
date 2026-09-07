#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "../src/ui/ui.h"
#include "../src/io/metro_io.h"

static int failures = 0;

#define CHECK(cond) do {                                                      \
    if (!(cond)) {                                                            \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
        failures++;                                                           \
    }                                                                         \
} while (0)

static void test_read_line(void) {
    /* 用临时文件重定向 stdin 模拟输入 */
    FILE *f = fopen("ui_input.txt", "wb");
    fputs("  体育西路  \r\n", f);       /* 首尾空白 + CRLF */
    fputs("\n", f);                     /* 空行 */
    fputs("公园前", f);                 /* 无换行结尾 */
    fclose(f);
    f = freopen("ui_input.txt", "rb", stdin);
    CHECK(f != NULL);

    char buf[128];
    CHECK(ui_read_line(buf, sizeof(buf)) == 0);
    CHECK(strcmp(buf, "体育西路") == 0);
    CHECK(ui_read_line(buf, sizeof(buf)) == 0);
    CHECK(strcmp(buf, "") == 0);
    CHECK(ui_read_line(buf, sizeof(buf)) == 0);
    CHECK(strcmp(buf, "公园前") == 0);
    CHECK(ui_read_line(buf, sizeof(buf)) == -1);   /* EOF */

    fclose(stdin);
    remove("ui_input.txt");
}

static void test_add_line_rollback(void) {
    /* 回归：加线中途区间输入非法 → 回滚必须同时清掉已提交的区间边，
     * 否则 metro_io_save 后数据被毒化，重新载入失败 */
    const char *dir = "ui_rollback_test";
    mkdir(dir);
    char path[256];

    snprintf(path, sizeof(path), "%s/stations.csv", dir);
    FILE *f = fopen(path, "wb");
    fputs("id,name,pinyin_name,en_name\n"
          "1,站A,zhana,Station A\n"
          "2,站B,zhanb,Station B\n"
          "3,站C,zhanc,Station C\n", f);
    fclose(f);
    snprintf(path, sizeof(path), "%s/lines.csv", dir);
    f = fopen(path, "wb");
    fputs("id,name,en_name,color,station_ids\n", f);
    fclose(f);
    snprintf(path, sizeof(path), "%s/edges.csv", dir);
    f = fopen(path, "wb");
    fputs("id,line_id,from_station_id,to_station_id,cost_time_second,cost_meters\n", f);
    fclose(f);

    Metro m;
    station_table_init(&m.stations);
    line_table_init(&m.lines);
    edge_table_init(&m.edges);
    CHECK(metro_io_load(dir, &m) == 0);

    ui_set_data_dir(dir);
    /* 维护 → 加线 → 测试线 → 颜色 → 站A → 站B → 站C → 空行结束 →
     * 区间1 "100,2000" 成功 → 区间2 "bad" 格式错误触发回滚 → 返回 → 退出 */
    f = freopen("ui_rollback_input.txt", "wb", stdin);
    fputs("3\n3\n测试线\n32\n站A\n站B\n站C\n\n100,2000\nbad\n5\n4\n", f);
    fclose(f);
    f = freopen("ui_rollback_input.txt", "rb", stdin);
    CHECK(f != NULL);
    ui_maintain(&m);
    fclose(stdin);

    /* 回滚后：无测试线、无任何边，保存后重新载入必须成功 */
    CHECK(line_find_by_name(&m.lines, "测试线") == NULL);
    CHECK(m.edges.rows.size == 0);
    CHECK(metro_io_save(dir, &m) == 0);

    Metro m2;
    station_table_init(&m2.stations);
    line_table_init(&m2.lines);
    edge_table_init(&m2.edges);
    CHECK(metro_io_load(dir, &m2) == 0);
    CHECK(m2.lines.rows.size == 0);
    CHECK(m2.edges.rows.size == 0);

    station_table_dispose(&m2.stations);
    line_table_dispose(&m2.lines);
    edge_table_dispose(&m2.edges);
    station_table_dispose(&m.stations);
    line_table_dispose(&m.lines);
    edge_table_dispose(&m.edges);

    snprintf(path, sizeof(path), "%s/stations.csv", dir);
    remove(path);
    snprintf(path, sizeof(path), "%s/lines.csv", dir);
    remove(path);
    snprintf(path, sizeof(path), "%s/edges.csv", dir);
    remove(path);
    remove(dir);
    remove("ui_rollback_input.txt");
}

int main(void) {
    test_read_line();
    test_add_line_rollback();
    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all ui checks passed\n");
    return 0;
}