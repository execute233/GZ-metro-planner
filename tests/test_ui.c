#include <stdio.h>
#include <string.h>
#include "../src/ui/ui.h"

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

int main(void) {
    test_read_line();
    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all ui checks passed\n");
    return 0;
}