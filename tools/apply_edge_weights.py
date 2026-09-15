"""Apply per-edge distance and run time to the atlas and its source JSON.

Usage: python tools/apply_edge_weights.py [table.tsv] [map.mbtiles]

The table is TSV with a header and the columns
    edge_id  line  from  to  meters  seconds  speed_kmh  source
Station names are checked against the database so a stale table is rejected
instead of silently writing wrong distances. Both the runtime atlas and
data/source/network.json are updated, because tools/build_mbtiles.py would
otherwise re-import the placeholder weights over them.

Close the application before running. `metro.mbtiles` is tracked by git, so
`git checkout -- metro.mbtiles` reverts the change.
"""
import csv
import json
import sqlite3
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_TABLE = ROOT / "tmp/edge_weights_final.tsv"
DEFAULT_ATLAS = ROOT / "metro.mbtiles"
SOURCE_JSON = ROOT / "data/source/network.json"

STATUS = ("distances read from the GBA track-layout diagram (配线图 v2.0.7); "
          "run times derived from per-line average travel speeds; "
          "requires visual/topology verification")


def read_table(path, atlas_stations, atlas_edges):
    """rows keyed by edge id, validated against the atlas"""
    rows, errors = {}, []
    with path.open(encoding="utf-8", newline="") as f:
        for r in csv.DictReader(f, delimiter="\t"):
            eid = int(r["edge_id"])
            edge = atlas_edges.get(eid)
            if not edge:
                errors.append(f"边 {eid} 不在图集中")
                continue
            _, fid, tid = edge
            if (r["from"], r["to"]) != (atlas_stations[fid], atlas_stations[tid]):
                errors.append(f"边 {eid} 端点不符：表 {r['from']}→{r['to']}，"
                              f"图集 {atlas_stations[fid]}→{atlas_stations[tid]}")
                continue
            if not r["meters"] or not r["seconds"]:
                errors.append(f"边 {eid} 缺少里程或时长")
                continue
            meters, seconds = int(r["meters"]), int(r["seconds"])
            if not 0 < meters <= 100000:
                errors.append(f"边 {eid} 里程 {meters} 超出校验范围 (0, 100000]")
                continue
            if not 0 <= seconds <= 100000:
                errors.append(f"边 {eid} 时长 {seconds} 超出校验范围 [0, 100000]")
                continue
            rows[eid] = (meters, seconds)
    return rows, errors


def main(argv):
    table = Path(argv[1]) if len(argv) > 1 else DEFAULT_TABLE
    atlas = Path(argv[2]) if len(argv) > 2 else DEFAULT_ATLAS
    if not table.exists():
        raise SystemExit(f"权重表不存在：{table}")
    if not atlas.exists():
        raise SystemExit(f"图集不存在：{atlas}")

    with sqlite3.connect(f"file:{atlas}?mode=ro", uri=True) as db:
        stations = {i: n for i, n in db.execute("SELECT id,name FROM stations")}
        edges = {i: (l, f, t) for i, l, f, t in
                 db.execute("SELECT id,line_id,from_id,to_id FROM edges")}

    rows, errors = read_table(table, stations, edges)
    if errors:
        for e in errors[:20]:
            print(f"  !! {e}")
        raise SystemExit(f"权重表校验失败，共 {len(errors)} 处，未写入任何数据")
    missing = sorted(set(edges) - set(rows))
    if missing:
        raise SystemExit(f"权重表缺少 {len(missing)} 条边：{missing[:20]}")

    with sqlite3.connect(f"file:{atlas}?mode=rw", uri=True) as db:
        db.execute("BEGIN IMMEDIATE")
        db.executemany("UPDATE edges SET meters=?, seconds=? WHERE id=?",
                       [(m, s, e) for e, (m, s) in rows.items()])
        db.execute("UPDATE metadata SET value=? WHERE name='description'",
                   ("Synthetic schematic coordinates; " + STATUS,))
        total = db.execute("SELECT COUNT(*),SUM(meters),SUM(seconds) FROM edges").fetchone()
        if db.execute("PRAGMA integrity_check").fetchone() != ("ok",):
            raise SystemExit("图集完整性检查失败")
        if db.execute("SELECT COUNT(*) FROM edges WHERE meters<=0 OR meters>100000 "
                      "OR seconds<0 OR seconds>100000").fetchone()[0]:
            raise SystemExit("写入后仍有越界权重")
    print(f"{atlas.name}: {len(rows)} 条边已更新，"
          f"合计 {total[1]/1000:.1f} km / {total[2]/60:.1f} min")

    source = json.loads(SOURCE_JSON.read_text(encoding="utf-8"))
    updated = 0
    for e in source["edges"]:
        if e["id"] in rows:
            e["meters"], e["seconds"] = rows[e["id"]]
            updated += 1
    unknown = [e["id"] for e in source["edges"] if e["id"] not in rows]
    if unknown:
        raise SystemExit(f"network.json 有 {len(unknown)} 条边不在权重表中：{unknown[:20]}")
    source["status"] = STATUS
    # indent=2 matches tools/prepare_network.py so the diff stays reviewable
    SOURCE_JSON.write_text(json.dumps(source, ensure_ascii=False, indent=2) + "\n",
                           encoding="utf-8")
    print(f"{SOURCE_JSON.relative_to(ROOT)}: {updated} 条边已更新")


if __name__ == "__main__":
    main(sys.argv)
