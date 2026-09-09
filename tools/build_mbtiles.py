"""Compile the editable network into standard gzip MVT/MBTiles + route tables.
Usage: python tools/build_mbtiles.py [source.json] [output.mbtiles]
"""

import json
import os
import sqlite3
import sys
from pathlib import Path

from map_tiles import encode_tiles

ROOT = Path(__file__).resolve().parents[1]
src = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "data/source/network.json"
out = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "metro.mbtiles"
d = json.loads(src.read_text(encoding="utf8"))
ids = {s["id"] for s in d["stations"]}
lids = {l["id"] for l in d["lines"]}
assert len(ids) == len(d["stations"]) and len(lids) == len(d["lines"])
assert all(0 <= s["x"] <= 4096 and 0 <= s["y"] <= 4096 for s in d["stations"])
assert len({e["id"] for e in d["edges"]}) == len(d["edges"])
for e in d["edges"]:
    assert e["from_id"] in ids and e["to_id"] in ids and e["line_id"] in lids
    assert e["meters"] > 0 and e["seconds"] >= 0 and len(e["points"]) >= 2
temp = out.with_suffix(".mbtiles.new")
if temp.exists():
    temp.unlink()
db = sqlite3.connect(temp)
db.executescript("""
CREATE TABLE metadata(name TEXT PRIMARY KEY,value TEXT NOT NULL);
CREATE TABLE tiles(zoom_level INTEGER,tile_column INTEGER,tile_row INTEGER,tile_data BLOB,
PRIMARY KEY(zoom_level,tile_column,tile_row));
CREATE TABLE stations(id INTEGER PRIMARY KEY,name TEXT,pinyin TEXT,initials TEXT,x REAL,y REAL,transfer INTEGER,en_name TEXT NOT NULL DEFAULT '');
CREATE TABLE lines(id INTEGER PRIMARY KEY,name TEXT,color INTEGER,en_name TEXT NOT NULL DEFAULT '',ansi_color INTEGER NOT NULL DEFAULT 37);
CREATE TABLE edges(id INTEGER PRIMARY KEY,line_id INTEGER,from_id INTEGER,to_id INTEGER,seconds INTEGER,meters INTEGER);
""")
meta = {
    "name": "广州地铁示意图",
    "format": "pbf",
    "type": "overlay",
    "version": "1.0",
    "minzoom": "0",
    "maxzoom": "5",
    "bounds": "-180,-85.05112878,180,85.05112878",
    "center": "0,0,0",
    "gzmp_schema": "2",
    "description": "Synthetic schematic coordinates; provisional 1000 m / 60 s per edge; "
    + d["status"],
    "json": json.dumps(
        {
            "vector_layers": [
                {
                    "id": "edges",
                    "fields": {"line_id": "Number"},
                    "minzoom": 0,
                    "maxzoom": 5,
                }
            ]
        }
    ),
}
db.executemany("INSERT INTO metadata VALUES(?,?)", meta.items())
db.executemany(
    "INSERT INTO stations(id,name,pinyin,initials,x,y,transfer) VALUES(?,?,?,?,?,?,?)",
    [
        (
            s["id"],
            s["name"],
            s["pinyin"],
            s["initials"],
            s["x"],
            s["y"],
            int(s["transfer"]),
        )
        for s in d["stations"]
    ],
)
db.executemany(
    "INSERT INTO lines(id,name,color) VALUES(?,?,?)",
    [(l["id"], l["name"], int(l["color"], 16)) for l in d["lines"]],
)
db.executemany(
    "INSERT INTO edges VALUES(?,?,?,?,?,?)",
    [
        (e["id"], e["line_id"], e["from_id"], e["to_id"], e["seconds"], e["meters"])
        for e in d["edges"]
    ],
)
db.executemany("INSERT INTO tiles VALUES(?,?,?,?)", encode_tiles(d["edges"]))
assert db.execute("PRAGMA integrity_check").fetchone()[0] == "ok"
db.commit()
db.close()
os.replace(temp, out)
print(out, out.stat().st_size, flush=True)
