#ifndef GZMP_UTF8_H
#define GZMP_UTF8_H
#include <stddef.h>
#include <stdint.h>
uint32_t utf8_decode(const char **text);
int utf8_encode(uint32_t cp, char out[5]);
int utf8_width(uint32_t cp);
size_t utf8_prev(const char *text, size_t cursor);
/* Cursor is a UTF-8 byte boundary. Insertion is atomic on capacity failure. */
int utf8_insert(char *text, size_t capacity, size_t *cursor, uint32_t cp);
void utf8_backspace(char *text, size_t *cursor);
#endif
