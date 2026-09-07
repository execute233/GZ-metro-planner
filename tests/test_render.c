#include <stdio.h>
#include "../src/render/render.h"

static int failures = 0;

#define CHECK(cond) do {                                                      \
    if (!(cond)) {                                                            \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
        failures++;                                                           \
    }                                                                         \
} while (0)

static void test_display_width(void) {
    CHECK(render_display_width("") == 0);
    CHECK(render_display_width("abc") == 3);
    CHECK(render_display_width("体育西路") == 8);          /* 4 个 CJK × 2 */
    CHECK(render_display_width("a体育") == 5);              /* 1 + 4 */
    CHECK(render_display_width("站A") == 3);                /* 2 + 1 */
    CHECK(render_display_width("1号线") == 5);              /* 1 + 2 + 2 */
    CHECK(render_display_width("Guangzhou") == 9);          /* 纯 ASCII */
}

int main(void) {
    test_display_width();
    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all render checks passed\n");
    return 0;
}