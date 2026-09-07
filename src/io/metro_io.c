#include "metro_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * metro_io —— CSV 持久化层实现
 *
 * 三个 CSV（stations / lines / edges）为系统的数据源，本模块负责：
 *   载入（读文件 → 解析 → 校验）与保存（内存 → 写回文件）。
 * 编码规范（UTF-8）：
 *   - 文件一律 UTF-8 无 BOM；载入时剥 BOM，避免首行首列带 \xEF\xBB\xBF 前缀；
 *   - 分隔符 , 与 ; 均为 ASCII，按字节解析安全；
 *   - 行尾 \r\n、行首尾空白统一去除。
 * 载入成功后数据已通过 metro_io_validate 一致性校验，可安全进入算法层。
 */

#define LINE_BUF_MAX 1024
#define COL_MAX      64

/* 去除行尾 \r\n 与首尾空白 */
static void trim(char *s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r' ||
                       s[len - 1] == ' ' || s[len - 1] == '\t'))
        s[--len] = '\0';
    char *p = s;
    while (*p == ' ' || *p == '\t')
        p++;
    if (p != s)
        memmove(s, p, strlen(p) + 1);
}

/* 剥 UTF-8 BOM */
static void strip_bom(char *line) {
    if ((unsigned char)line[0] == 0xEF &&
        (unsigned char)line[1] == 0xBB &&
        (unsigned char)line[2] == 0xBF)
        memmove(line, line + 3, strlen(line + 3) + 1);
}

/* 按分隔符切分，返回第 idx 个字段（0 起）的副本；不存在返回 0 */
static int split_field(const char *line, char sep, int idx, char *out, size_t out_size) {
    const char *start = line;
    int cur = 0;
    for (;;) {
        const char *end = strchr(start, sep);
        if (end == NULL)
            end = start + strlen(start);
        if (cur == idx) {
            size_t n = (size_t)(end - start);
            if (n >= out_size)
                n = out_size - 1;
            memcpy(out, start, n);
            out[n] = '\0';
            return 1;
        }
        if (*end == '\0')
            return 0;
        start = end + 1;
        cur++;
    }
}

static int parse_int_field(const char *line, char sep, int idx, int *out) {
    char buf[COL_MAX];
    if (!split_field(line, sep, idx, buf, sizeof(buf)))
        return -1;
    *out = atoi(buf);
    return 0;
}

static int read_stations(const char *path, StationTable *t) {
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return -1;
    char line[LINE_BUF_MAX];
    int first = 1;
    while (fgets(line, sizeof(line), f)) {
        strip_bom(line);
        trim(line);
        if (first) {   /* 跳过表头 */
            first = 0;
            continue;
        }
        if (line[0] == '\0')
            continue;
        Station st;
        memset(&st, 0, sizeof(st));
        if (parse_int_field(line, ',', 0, &st.id) != 0)
            continue;
        split_field(line, ',', 1, st.name, sizeof(st.name));
        split_field(line, ',', 2, st.pinyin_name, sizeof(st.pinyin_name));
        split_field(line, ',', 3, st.en_name, sizeof(st.en_name));
        al_station_push(&t->rows, st);
    }
    fclose(f);
    return 0;
}

static int read_lines(const char *path, LineTable *t) {
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return -1;
    char line[LINE_BUF_MAX];
    int first = 1;
    while (fgets(line, sizeof(line), f)) {
        strip_bom(line);
        trim(line);
        if (first) {
            first = 0;
            continue;
        }
        if (line[0] == '\0')
            continue;
        Line ln;
        memset(&ln, 0, sizeof(ln));
        al_int_init(&ln.station_ids);
        if (parse_int_field(line, ',', 0, &ln.id) != 0) {
            al_int_dispose(&ln.station_ids);
            continue;
        }
        split_field(line, ',', 1, ln.name, sizeof(ln.name));
        split_field(line, ',', 2, ln.en_name, sizeof(ln.en_name));
        parse_int_field(line, ',', 3, &ln.color);

        char ids_col[LINE_BUF_MAX];
        if (split_field(line, ',', 4, ids_col, sizeof(ids_col))) {
            const char *p = ids_col;
            while (*p) {
                char *end;
                long id = strtol(p, &end, 10);
                if (end == p)
                    break;
                al_int_push(&ln.station_ids, (int)id);
                p = end;
                if (*p == ';')
                    p++;
            }
        }
        al_line_push(&t->rows, ln);
    }
    fclose(f);
    return 0;
}

static int read_edges(const char *path, EdgeTable *t) {
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return -1;
    char line[LINE_BUF_MAX];
    int first = 1;
    while (fgets(line, sizeof(line), f)) {
        strip_bom(line);
        trim(line);
        if (first) {
            first = 0;
            continue;
        }
        if (line[0] == '\0')
            continue;
        Edge e;
        memset(&e, 0, sizeof(e));
        if (parse_int_field(line, ',', 0, &e.id) != 0)
            continue;
        parse_int_field(line, ',', 1, &e.line_id);
        parse_int_field(line, ',', 2, &e.from_station_id);
        parse_int_field(line, ',', 3, &e.to_station_id);
        parse_int_field(line, ',', 4, &e.cost_time_second);
        parse_int_field(line, ',', 5, &e.cost_meters);
        al_edge_push(&t->rows, e);
    }
    fclose(f);
    return 0;
}

/* ---- 校验 ---- */

/* 三表主键 id 是否唯一（两两比较，规模小直接 O(n²)） */
static int ids_unique(const StationTable *stations, const LineTable *lines,
                      const EdgeTable *edges) {
    for (size_t i = 0; i < stations->rows.size; i++)
        for (size_t j = i + 1; j < stations->rows.size; j++)
            if (stations->rows.items[i].id == stations->rows.items[j].id)
                return 0;
    for (size_t i = 0; i < lines->rows.size; i++)
        for (size_t j = i + 1; j < lines->rows.size; j++)
            if (lines->rows.items[i].id == lines->rows.items[j].id)
                return 0;
    for (size_t i = 0; i < edges->rows.size; i++)
        for (size_t j = i + 1; j < edges->rows.size; j++)
            if (edges->rows.items[i].id == edges->rows.items[j].id)
                return 0;
    return 1;
}

/* 相邻两站是否在指定线路的站序中相邻出现 */
static int line_has_adjacent(const Line *ln, int a, int b) {
    for (size_t i = 0; i + 1 < ln->station_ids.size; i++) {
        if ((ln->station_ids.items[i] == a && ln->station_ids.items[i + 1] == b) ||
            (ln->station_ids.items[i] == b && ln->station_ids.items[i + 1] == a))
            return 1;
    }
    return 0;
}

/*
 * 一致性校验（载入后的守门员）：任一条不满足即返回 -1 并写明原因。
 *   1. 三表主键 id 唯一，且 id 必须为正数（负数 id 会让 graph_build 数组越界写）；
 *   2. 线路站序、边引用的站 id 必须存在；边的 line_id 必须存在；
 *   3. 边两端必须是该线站序中的相邻站（边与站序双向一致）；
 *   4. cost_meters > 0、cost_time_second >= 0；
 *   5. 同一线路内不允许 from/to 互换的重复边。
 */
int metro_io_validate(const Metro *metro, char *errbuf, size_t errbuf_size) {
    const StationTable *stations = &metro->stations;
    const LineTable *lines = &metro->lines;
    const EdgeTable *edges = &metro->edges;

    if (!ids_unique(stations, lines, edges)) {
        snprintf(errbuf, errbuf_size, "主键 id 重复");
        return -1;
    }
    for (size_t i = 0; i < stations->rows.size; i++) {
        if (stations->rows.items[i].id < 1) {
            snprintf(errbuf, errbuf_size, "站点 id 必须为正数（第 %zu 条）", i + 1);
            return -1;
        }
    }
    for (size_t i = 0; i < lines->rows.size; i++) {
        if (lines->rows.items[i].id < 1) {
            snprintf(errbuf, errbuf_size, "线路 id 必须为正数（第 %zu 条）", i + 1);
            return -1;
        }
    }
    for (size_t i = 0; i < edges->rows.size; i++) {
        if (edges->rows.items[i].id < 1 ||
            edges->rows.items[i].line_id < 1) {
            snprintf(errbuf, errbuf_size, "边 id/line_id 必须为正数（第 %zu 条）", i + 1);
            return -1;
        }
    }
    for (size_t i = 0; i < lines->rows.size; i++) {
        const Line *ln = &lines->rows.items[i];
        for (size_t k = 0; k < ln->station_ids.size; k++) {
            if (station_find_by_id(stations, ln->station_ids.items[k]) == NULL) {
                snprintf(errbuf, errbuf_size, "线路 %s 引用不存在的站 id=%d",
                         ln->name, ln->station_ids.items[k]);
                return -1;
            }
        }
    }
    for (size_t i = 0; i < edges->rows.size; i++) {
        const Edge *e = &edges->rows.items[i];
        if (station_find_by_id(stations, e->from_station_id) == NULL ||
            station_find_by_id(stations, e->to_station_id) == NULL) {
            snprintf(errbuf, errbuf_size, "边 %d 引用不存在的站", e->id);
            return -1;
        }
        Line *ln = line_find_by_id(lines, e->line_id);
        if (ln == NULL) {
            snprintf(errbuf, errbuf_size, "边 %d 引用不存在的线路 %d", e->id, e->line_id);
            return -1;
        }
        if (!line_has_adjacent(ln, e->from_station_id, e->to_station_id)) {
            snprintf(errbuf, errbuf_size, "边 %d 两端不是线路 %s 的相邻站",
                     e->id, ln->name);
            return -1;
        }
        if (e->cost_meters <= 0 || e->cost_time_second < 0) {
            snprintf(errbuf, errbuf_size, "边 %d 里程或时长非法", e->id);
            return -1;
        }
    }
    for (size_t i = 0; i < edges->rows.size; i++) {
        const Edge *a = &edges->rows.items[i];
        for (size_t j = i + 1; j < edges->rows.size; j++) {
            const Edge *b = &edges->rows.items[j];
            if (a->line_id == b->line_id &&
                ((a->from_station_id == b->from_station_id &&
                  a->to_station_id == b->to_station_id) ||
                 (a->from_station_id == b->to_station_id &&
                  a->to_station_id == b->from_station_id))) {
                snprintf(errbuf, errbuf_size, "线路 %d 存在重复区间边 %d/%d",
                         a->line_id, a->id, b->id);
                return -1;
            }
        }
    }
    return 0;
}

/* ---- 载入 ---- */

int metro_io_load(const char *data_dir, Metro *metro) {
    char path[512];
    snprintf(path, sizeof(path), "%s/stations.csv", data_dir);
    if (read_stations(path, &metro->stations) != 0)
        return -1;
    snprintf(path, sizeof(path), "%s/lines.csv", data_dir);
    if (read_lines(path, &metro->lines) != 0)
        return -1;
    snprintf(path, sizeof(path), "%s/edges.csv", data_dir);
    if (read_edges(path, &metro->edges) != 0)
        return -1;

    char errbuf[256];
    if (metro_io_validate(metro, errbuf, sizeof(errbuf)) != 0)
        return -1;
    return 0;
}

/* ---- 保存 ---- */

static int write_stations(const char *path, const StationTable *t) {
    FILE *f = fopen(path, "wb");
    if (f == NULL)
        return -1;
    fprintf(f, "id,name,pinyin_name,en_name\n");
    for (size_t i = 0; i < t->rows.size; i++)
        fprintf(f, "%d,%s,%s,%s\n", t->rows.items[i].id, t->rows.items[i].name,
                t->rows.items[i].pinyin_name, t->rows.items[i].en_name);
    fclose(f);
    return 0;
}

static int write_lines(const char *path, const LineTable *t) {
    FILE *f = fopen(path, "wb");
    if (f == NULL)
        return -1;
    fprintf(f, "id,name,en_name,color,station_ids\n");
    for (size_t i = 0; i < t->rows.size; i++) {
        const Line *ln = &t->rows.items[i];
        fprintf(f, "%d,%s,%s,%d,", ln->id, ln->name, ln->en_name, ln->color);
        for (size_t k = 0; k < ln->station_ids.size; k++) {
            if (k > 0)
                fputc(';', f);
            fprintf(f, "%d", ln->station_ids.items[k]);
        }
        fputc('\n', f);
    }
    fclose(f);
    return 0;
}

static int write_edges(const char *path, const EdgeTable *t) {
    FILE *f = fopen(path, "wb");
    if (f == NULL)
        return -1;
    fprintf(f, "id,line_id,from_station_id,to_station_id,cost_time_second,cost_meters\n");
    for (size_t i = 0; i < t->rows.size; i++) {
        const Edge *e = &t->rows.items[i];
        fprintf(f, "%d,%d,%d,%d,%d,%d\n", e->id, e->line_id, e->from_station_id,
                e->to_station_id, e->cost_time_second, e->cost_meters);
    }
    fclose(f);
    return 0;
}

int metro_io_save(const char *data_dir, const Metro *metro) {
    char path[512];
    snprintf(path, sizeof(path), "%s/stations.csv", data_dir);
    if (write_stations(path, &metro->stations) != 0)
        return -1;
    snprintf(path, sizeof(path), "%s/lines.csv", data_dir);
    if (write_lines(path, &metro->lines) != 0)
        return -1;
    snprintf(path, sizeof(path), "%s/edges.csv", data_dir);
    if (write_edges(path, &metro->edges) != 0)
        return -1;
    return 0;
}