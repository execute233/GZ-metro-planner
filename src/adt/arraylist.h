#ifndef GZMP_ADT_ARRAYLIST_H
#define GZMP_ADT_ARRAYLIST_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/*
 * ArrayList —— 自研动态数组（宏生成类型化实现）
 *
 * C 语言没有原生泛型，本项目用"宏 + 粘贴"生成类型安全的动态数组：
 *   每个实例是一组结构体 + static inline 函数，独立命名空间互不冲突。
 *   扩容策略：初始容量 4，不足时倍增（realloc），均摊 O(1) 追加。
 *
 * 用法示例：
 *   DEFINE_ARRAYLIST(int, Int, int)        —— 生成类型 ArrayList_Int、函数 al_int_*
 *   ArrayList_Int list;  al_int_init(&list);
 *   al_int_push(&list, 42);                —— 追加
 *   int *p = al_int_get(&list, 0);         —— 取元素指针（越界返回 NULL）
 *   al_int_dispose(&list);                 —— 释放，之后可再次 init 复用
 *
 * 参数约定：
 *   TYPE —— 元素类型；NAME —— 类型名后缀（生成 ArrayList_##NAME）；
 *   PFX  —— 小写函数前缀（生成 al_##PFX##_*）。
 *   C 预处理器无法做大小写转换，故 NAME 与 PFX 需分别给出。
 */
#define DEFINE_ARRAYLIST(TYPE, NAME, PFX)                                   \
    typedef struct {                                                        \
        TYPE  *items;      /* 元素存储区，realloc 动态扩容 */               \
        size_t size;       /* 当前元素个数 */                               \
        size_t capacity;   /* 已分配容量 */                                 \
    } ArrayList_##NAME;                                                     \
                                                                            \
    /* 初始化置空，使用前必须先调用 */                                      \
    static inline int al_##PFX##_init(ArrayList_##NAME *self) {            \
        self->items = NULL;                                                 \
        self->size = 0;                                                     \
        self->capacity = 0;                                                 \
        return 0;                                                           \
    }                                                                       \
                                                                            \
    /* 释放内部数组，之后可再次 init 复用 */                                \
    static inline void al_##PFX##_dispose(ArrayList_##NAME *self) {        \
        free(self->items);                                                  \
        self->items = NULL;                                                 \
        self->size = 0;                                                     \
        self->capacity = 0;                                                 \
    }                                                                       \
                                                                            \
    /* 扩容到至少 need 个元素，返回 0 成功 / -1 分配失败 */                 \
    static inline int al_##PFX##_reserve(ArrayList_##NAME *self,           \
                                          size_t need) {                    \
        if (need <= self->capacity) return 0;                               \
        size_t cap = self->capacity ? self->capacity : 4;                   \
        while (cap < need) cap <<= 1;                                       \
        TYPE *p = realloc(self->items, cap * sizeof(TYPE));                 \
        if (p == NULL) return -1;                                           \
        self->items = p;                                                    \
        self->capacity = cap;                                               \
        return 0;                                                           \
    }                                                                       \
                                                                            \
    /* 追加元素 val，返回 0 成功 / -1 失败 */                               \
    static inline int al_##PFX##_push(ArrayList_##NAME *self, TYPE val) {  \
        if (al_##PFX##_reserve(self, self->size + 1) != 0) return -1;      \
        self->items[self->size++] = val;                                    \
        return 0;                                                           \
    }                                                                       \
                                                                            \
    /* 取下标 index 的元素指针，越界返回 NULL */                            \
    static inline TYPE *al_##PFX##_get(ArrayList_##NAME *self,             \
                                        size_t index) {                     \
        return index < self->size ? &self->items[index] : NULL;             \
    }                                                                       \
                                                                            \
    /* 删除下标 index 的元素并前移填补，返回 0 成功 / -1 越界 */            \
    static inline int al_##PFX##_remove_at(ArrayList_##NAME *self,         \
                                            size_t index) {                 \
        if (index >= self->size) return -1;                                 \
        memmove(&self->items[index], &self->items[index + 1],               \
                (self->size - index - 1) * sizeof(TYPE));                   \
        self->size--;                                                       \
        return 0;                                                           \
    }

/* 内置 int 实例：Line.station_ids、图邻接表通用 */
DEFINE_ARRAYLIST(int, Int, int)

#endif