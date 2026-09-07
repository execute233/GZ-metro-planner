#include <stdio.h>
#include <string.h>
#include "../src/adt/line.h"

static int failures = 0;

#define CHECK(cond) do {                                                      \
    if (!(cond)) {                                                            \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
        failures++;                                                           \
    }                                                                         \
} while (0)

static Line make_line(const char *name, const char *en, int color,
                      const int *ids, size_t n) {
    Line ln;
    memset(&ln, 0, sizeof(ln));
    strcpy(ln.name, name);
    strcpy(ln.en_name, en);
    ln.color = color;
    al_int_init(&ln.station_ids);
    for (size_t i = 0; i < n; i++)
        al_int_push(&ln.station_ids, ids[i]);
    return ln;
}

static void test_add_and_deep_copy(void) {
    LineTable t;
    line_table_init(&t);

    int ids1[] = {1, 2, 3};
    Line ln = make_line("1号线", "Line 1", 31, ids1, 3);
    CHECK(line_add(&t, &ln) == 0);
    CHECK(ln.id == 1);

    al_int_push(&ln.station_ids, 99);
    Line *stored = line_find_by_id(&t, 1);
    CHECK(stored != NULL);
    CHECK(stored->station_ids.size == 3);   /* 深拷贝：外部改动不影响表内 */
    CHECK(*al_int_get(&stored->station_ids, 2) == 3);

    al_int_dispose(&ln.station_ids);
    line_table_dispose(&t);
}

static void test_find_by_name(void) {
    LineTable t;
    line_table_init(&t);
    int ids1[] = {1, 2};
    Line ln = make_line("1号线", "Line 1", 31, ids1, 2);
    line_add(&t, &ln);

    CHECK(line_find_by_name(&t, "1号线") != NULL);
    CHECK(line_find_by_name(&t, "不存在线") == NULL);

    al_int_dispose(&ln.station_ids);
    line_table_dispose(&t);
}

static void test_remove_and_next_id(void) {
    LineTable t;
    line_table_init(&t);
    int ids1[] = {1};
    int ids2[] = {2};
    int ids3[] = {3};
    Line a = make_line("A线", "Line A", 31, ids1, 1);
    Line b = make_line("B线", "Line B", 34, ids2, 1);
    Line c = make_line("C线", "Line C", 33, ids3, 1);
    line_add(&t, &a);
    line_add(&t, &b);
    line_add(&t, &c);
    al_int_dispose(&a.station_ids);
    al_int_dispose(&b.station_ids);
    al_int_dispose(&c.station_ids);

    CHECK(line_remove(&t, 2) == 0);
    CHECK(t.rows.size == 2);
    CHECK(line_find_by_id(&t, 2) == NULL);
    CHECK(line_remove(&t, 2) == -1);

    int ids4[] = {4};
    Line d = make_line("D线", "Line D", 32, ids4, 1);
    line_add(&t, &d);
    CHECK(d.id == 4);   /* 删除不重用 */
    al_int_dispose(&d.station_ids);

    line_table_dispose(&t);
}

int main(void) {
    test_add_and_deep_copy();
    test_find_by_name();
    test_remove_and_next_id();
    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all line checks passed\n");
    return 0;
}