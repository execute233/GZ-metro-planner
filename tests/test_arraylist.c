#include <stdio.h>
#include "../src/adt/arraylist.h"

static int failures = 0;

#define CHECK(cond) do {                                                      \
    if (!(cond)) {                                                            \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
        failures++;                                                           \
    }                                                                         \
} while (0)

static void test_push_get(void) {
    ArrayList_Int list;
    al_int_init(&list);
    CHECK(list.size == 0);
    CHECK(al_int_push(&list, 42) == 0);
    CHECK(al_int_push(&list, 7) == 0);
    CHECK(list.size == 2);
    CHECK(*al_int_get(&list, 0) == 42);
    CHECK(*al_int_get(&list, 1) == 7);
    CHECK(al_int_get(&list, 2) == NULL);
    al_int_dispose(&list);
    CHECK(list.size == 0);
}

static void test_reserve_growth(void) {
    ArrayList_Int list;
    al_int_init(&list);
    for (int i = 0; i < 1000; i++)
        CHECK(al_int_push(&list, i) == 0);
    CHECK(list.size == 1000);
    for (int i = 0; i < 1000; i++)
        CHECK(*al_int_get(&list, (size_t)i) == i);
    al_int_dispose(&list);
}

static void test_remove_at(void) {
    ArrayList_Int list;
    al_int_init(&list);
    for (int i = 0; i < 5; i++)
        al_int_push(&list, i);
    CHECK(al_int_remove_at(&list, 1) == 0);
    CHECK(list.size == 4);
    CHECK(*al_int_get(&list, 1) == 2);
    CHECK(al_int_remove_at(&list, 10) == -1);
    CHECK(al_int_remove_at(&list, 4) == -1);
    al_int_dispose(&list);
}

int main(void) {
    test_push_get();
    test_reserve_growth();
    test_remove_at();
    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all arraylist checks passed\n");
    return 0;
}