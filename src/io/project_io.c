#include "project_io.h"
#include "metro_io.h"
#include <errno.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#endif

static const char *files[] = {"stations.csv", "lines.csv", "edges.csv", "map_stations.csv", "map_segments.csv", "map_styles.csv"};
static int path_for(char out[1024], const char *dir, const char *name, const char *suffix) {
    return snprintf(out, 1024, "%s/%s%s", dir, name, suffix) >= 1024 ? -1 : 0;
}
static int replace_file(const char *from, const char *to) {
#ifdef _WIN32
    return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ? 0 : -1;
#else
    return rename(from, to);
#endif
}
static int finish(FILE *f, int bad) {
    if (fflush(f)) bad = 1;
#ifdef _WIN32
    if (_commit(_fileno(f))) bad = 1;
#endif
    if (fclose(f)) bad = 1;
    return bad ? -1 : 0;
}
static int copy_file(const char *from, const char *to) {
    FILE *in = fopen(from, "rb");
    if (!in) return -1;
    FILE *out = fopen(to, "wb");
    if (!out) { fclose(in); return -1; }
    char buf[4096]; size_t n; int bad = 0;
    while ((n = fread(buf, 1, sizeof(buf), in))) if (fwrite(buf, 1, n, out) != n) { bad = 1; break; }
    if (ferror(in)) bad = 1;
    if (fclose(in)) bad = 1;
    return finish(out, bad);
}
static void cleanup(const char *dir) {
    char path[1024];
    for (int i = 0; i < 6; i++) {
        path_for(path, dir, files[i], ".gzmp-new"); remove(path);
        path_for(path, dir, files[i], ".gzmp-old"); remove(path);
    }
    path_for(path, dir, ".gzmp-transaction", ".new"); remove(path);
}
int project_io_recover(const char *dir, char *error, size_t cap) {
    char marker[1024], path[1024], backup[1024], restore[1024];
    if (path_for(marker, dir, ".gzmp-transaction", "")) goto failed;
    FILE *f = fopen(marker, "rb");
    if (!f) { if (errno == ENOENT) return 0; goto failed; }
    unsigned mask; char extra;
    int valid = fscanf(f, "GZMP1 %u %c", &mask, &extra) == 1 && mask < 64;
    if (fclose(f)) valid = 0;
    if (!valid) goto failed;
    for (int i = 0; i < 6; i++) {
        path_for(path, dir, files[i], ""); path_for(backup, dir, files[i], ".gzmp-old");
        path_for(restore, dir, files[i], ".gzmp-new");
        if (mask & (1u << i)) {
            /* Keep backups until the marker is removed: recovery is idempotent. */
            if (copy_file(backup, restore) || replace_file(restore, path)) goto failed;
        } else if (remove(path) && errno != ENOENT) goto failed;
    }
    if (remove(marker)) goto failed;
    cleanup(dir);
    return 0;
failed: snprintf(error, cap, "保存事务恢复失败；保留备份，请检查目录权限：%s", dir); return -1;
}
static int write_metro(FILE *f, const Metro *m, int table) {
    if (table == 0) {
        fprintf(f, "id,name,pinyin_name,en_name\n");
        for (size_t i = 0; i < m->stations.rows.size; i++) {
            const Station *s = &m->stations.rows.items[i];
            fprintf(f, "%d,%s,%s,%s\n", s->id, s->name, s->pinyin_name, s->en_name);
        }
    } else if (table == 1) {
        fprintf(f, "id,name,en_name,color,station_ids\n");
        for (size_t i = 0; i < m->lines.rows.size; i++) {
            const Line *l = &m->lines.rows.items[i];
            fprintf(f, "%d,%s,%s,%d,", l->id, l->name, l->en_name, l->color);
            for (size_t j = 0; j < l->station_ids.size; j++) fprintf(f, "%s%d", j ? ";" : "", l->station_ids.items[j]);
            fputc('\n', f);
        }
    } else {
        fprintf(f, "id,line_id,from_station_id,to_station_id,cost_time_second,cost_meters\n");
        for (size_t i = 0; i < m->edges.rows.size; i++) {
            const Edge *e = &m->edges.rows.items[i];
            fprintf(f, "%d,%d,%d,%d,%d,%d\n", e->id, e->line_id, e->from_station_id, e->to_station_id, e->cost_time_second, e->cost_meters);
        }
    }
    return ferror(f) ? -1 : 0;
}
int project_io_save(const char *dir, const Metro *metro, const MapDocument *map, char *error, size_t cap) {
    if (strlen(dir) > 900) { snprintf(error, cap, "数据目录路径过长"); return -1; }
    if (metro_io_validate(metro, error, cap) || map_io_validate(metro, map, error, cap) || project_io_recover(dir, error, cap)) return -1;
    char path[1024], temp[1024], backup[1024], marker[1024], marker_temp[1024];
    unsigned mask = 0;
    for (int i = 0; i < 6; i++) {
        path_for(path, dir, files[i], ""); path_for(temp, dir, files[i], ".gzmp-new"); path_for(backup, dir, files[i], ".gzmp-old");
        FILE *original = fopen(path, "rb");
        if (original) {
            fclose(original); mask |= 1u << i;
            if (copy_file(path, backup)) goto failed;
        } else if (errno != ENOENT) goto failed;
        FILE *f = fopen(temp, "wb");
        if (!f) goto failed;
        int bad = i < 3 ? write_metro(f, metro, i) : map_io_write(f, map, i-3);
        if (finish(f, bad)) goto failed;
    }
    path_for(marker, dir, ".gzmp-transaction", ""); path_for(marker_temp, dir, ".gzmp-transaction", ".new");
    FILE *f = fopen(marker_temp, "wb");
    if (!f) goto failed;
    if (finish(f, fprintf(f, "GZMP1 %u\n", mask) < 0)) goto failed;
    if (replace_file(marker_temp, marker)) goto failed;
    for (int i = 0; i < 6; i++) {
        path_for(path, dir, files[i], ""); path_for(temp, dir, files[i], ".gzmp-new");
        if (replace_file(temp, path)) goto rollback;
    }
    if (remove(marker)) goto rollback;
    cleanup(dir); return 0;
rollback:
    if (project_io_recover(dir, error, cap)) return -1;
failed:
    snprintf(error, cap, "保存失败，原数据未提交；请检查目录权限或磁盘空间"); return -1;
}
void project_dispose(Metro *m) { station_table_dispose(&m->stations); line_table_dispose(&m->lines); edge_table_dispose(&m->edges); }
int project_clone(Metro *out, const Metro *source) {
    *out = (Metro){0};
    for (size_t i = 0; i < source->stations.rows.size; i++) if (al_station_push(&out->stations.rows, source->stations.rows.items[i])) goto failed;
    for (size_t i = 0; i < source->edges.rows.size; i++) if (al_edge_push(&out->edges.rows, source->edges.rows.items[i])) goto failed;
    for (size_t i = 0; i < source->lines.rows.size; i++) {
        Line l = source->lines.rows.items[i]; l.station_ids = (ArrayList_Int){0};
        for (size_t j = 0; j < source->lines.rows.items[i].station_ids.size; j++)
            if (al_int_push(&l.station_ids, source->lines.rows.items[i].station_ids.items[j])) { al_int_dispose(&l.station_ids); goto failed; }
        if (al_line_push(&out->lines.rows, l)) { al_int_dispose(&l.station_ids); goto failed; }
    }
    return 0;
failed: project_dispose(out); return -1;
}
