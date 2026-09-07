#include "utf8.h"
#include <string.h>

uint32_t utf8_decode(const char **text) {
    const unsigned char *p = (const unsigned char *)*text;
    if (!*p) return 0;
    uint32_t cp = *p;
    int n = cp < 128 ? 1 : cp >= 0xc2 && cp <= 0xdf ? 2 :
            cp >= 0xe0 && cp <= 0xef ? 3 : cp >= 0xf0 && cp <= 0xf4 ? 4 : 0;
    if (!n) { (*text)++; return 0xfffd; }
    if (n > 1) cp &= (1u << (7 - n)) - 1;
    for (int i = 1; i < n; i++) {
        if ((p[i] & 0xc0) != 0x80) { (*text)++; return 0xfffd; }
        cp = (cp << 6) | (p[i] & 63);
    }
    if ((n == 2 && cp < 128) || (n == 3 && cp < 2048) ||
        (n == 4 && cp < 65536) || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) {
        (*text)++; return 0xfffd;
    }
    *text += n;
    return cp;
}

int utf8_encode(uint32_t cp, char out[5]) {
    int n = 0;
    if (cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return 0;
    if (cp < 128) out[n++] = (char)cp;
    else if (cp < 2048) { out[n++] = (char)(0xc0 | (cp >> 6)); out[n++] = (char)(0x80 | (cp & 63)); }
    else if (cp < 65536) {
        out[n++] = (char)(0xe0 | (cp >> 12)); out[n++] = (char)(0x80 | ((cp >> 6) & 63)); out[n++] = (char)(0x80 | (cp & 63));
    } else {
        out[n++] = (char)(0xf0 | (cp >> 18)); out[n++] = (char)(0x80 | ((cp >> 12) & 63));
        out[n++] = (char)(0x80 | ((cp >> 6) & 63)); out[n++] = (char)(0x80 | (cp & 63));
    }
    out[n] = 0;
    return n;
}

int utf8_width(uint32_t cp) {
    if (!cp || cp < 32 || (cp >= 127 && cp < 160)) return 0;
    if ((cp >= 0x300 && cp <= 0x36f) || (cp >= 0xfe00 && cp <= 0xfe0f) || cp == 0x200d) return 0;
    if (cp >= 0x1100 && (cp <= 0x115f || cp == 0x2329 || cp == 0x232a ||
        (cp >= 0x2e80 && cp <= 0xa4cf && cp != 0x303f) || (cp >= 0xac00 && cp <= 0xd7a3) ||
        (cp >= 0xf900 && cp <= 0xfaff) || (cp >= 0xfe10 && cp <= 0xfe19) ||
        (cp >= 0xfe30 && cp <= 0xfe6f) || (cp >= 0xff01 && cp <= 0xff60) ||
        (cp >= 0xffe0 && cp <= 0xffe6) || (cp >= 0x1f300 && cp <= 0x1faff) || cp >= 0x20000)) return 2;
    return 1;
}

size_t utf8_prev(const char *text, size_t cursor) {
    if (cursor) cursor--;
    while (cursor && ((unsigned char)text[cursor] & 0xc0) == 0x80) cursor--;
    return cursor;
}
int utf8_insert(char *text, size_t capacity, size_t *cursor, uint32_t cp) {
    char bytes[5];
    int n = utf8_encode(cp, bytes);
    size_t len = strlen(text);
    if (!n || !cp || cp < 32 || (cp >= 127 && cp < 160) || len + (size_t)n >= capacity || *cursor > len) return -1;
    memmove(text + *cursor + n, text + *cursor, len - *cursor + 1);
    memcpy(text + *cursor, bytes, n);
    *cursor += n;
    return 0;
}
void utf8_backspace(char *text, size_t *cursor) {
    size_t prev = utf8_prev(text, *cursor);
    memmove(text + prev, text + *cursor, strlen(text + *cursor) + 1);
    *cursor = prev;
}
