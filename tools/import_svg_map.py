"""Atomically import calibrated SVG geometry into an existing schema-2 atlas."""

import argparse
import hashlib
import json
import math
import re
import sqlite3
import uuid
import xml.etree.ElementTree as ET
from contextlib import closing
from datetime import datetime, timezone
from pathlib import Path

from create_svg_trial import ROOT, SVG
from map_tiles import encode_tiles
from svg_geometry import flatten_path, join_station_centers

SIDES = ("E", "W", "N", "S", "NE", "NW", "SE", "SW")
NS = {"s": SVG}


def read_artwork(path):
    if path.stat().st_size > 32 * 1024 * 1024:
        raise ValueError("SVG exceeds 32 MiB")
    root = ET.parse(path).getroot()
    if root.attrib.get("viewBox") != "0 0 4096 4096":
        raise ValueError("Expected a 4096px schematic")
    if any("transform" in e.attrib for e in root.iter()):
        raise ValueError("Flatten SVG transforms to path coordinates before importing")
    object_ids = [e.attrib["id"] for e in root.iter() if "id" in e.attrib]
    if len(object_ids) != len(set(object_ids)):
        raise ValueError("Duplicate SVG object IDs")
    anchors, colors, edges, labels = {}, {}, {}, {}
    for c in root.findall(".//s:circle", NS):
        sid, lid = int(c.attrib["data-station-id"]), int(c.attrib["data-line-id"])
        p = complex(float(c.attrib["cx"]), float(c.attrib["cy"]))
        if not all(math.isfinite(v) and 0 <= v <= 4096 for v in (p.real, p.imag)):
            raise ValueError("Invalid station coordinate")
        if (sid, lid) in anchors:
            raise ValueError("Duplicate station/line anchor")
        anchors[sid, lid] = p
    for group in root.findall("s:g", NS):
        if not re.fullmatch(r"line-\d+", group.attrib.get("id", "")):
            continue
        lid = int(group.attrib["id"][5:])
        match = re.search(
            r"(?:^|;)stroke:#([0-9a-fA-F]{6})(?:;|$)", group.attrib.get("style", "")
        )
        if not match:
            raise ValueError(f"Missing RGB stroke on line {lid}")
        colors[lid] = int(match[1], 16)
        for p in group.findall("s:path", NS):
            eid = int(p.attrib["id"].removeprefix("edge-"))
            a, b = int(p.attrib["data-from-id"]), int(p.attrib["data-to-id"])
            if int(p.attrib["data-line-id"]) != lid or eid in edges:
                raise ValueError("Invalid edge/line ID")
            points = flatten_path(p.attrib["d"])
            if not all(0 <= v <= 4096 for p in points for v in (p.real, p.imag)):
                raise ValueError("SVG edge outside schematic bounds")
            if (
                abs(points[0] - anchors[a, lid]) > 0.02
                or abs(points[-1] - anchors[b, lid]) > 0.02
            ):
                raise ValueError(f"Edge {eid} does not meet its SVG station anchors")
            edges[eid] = {
                "id": eid,
                "line_id": lid,
                "from_id": a,
                "to_id": b,
                "curve": points,
            }
    for text in root.findall("s:g[@id='station-labels']/s:text", NS):
        sid = int(text.attrib["data-station-id"])
        if sid in labels or text.attrib.get("data-placement") not in SIDES:
            raise ValueError("Missing or duplicate label placement")
        labels[sid] = (
            "".join(text.itertext()),
            SIDES.index(text.attrib["data-placement"]) + 1,
        )
    station_ids = {sid for sid, _ in anchors}
    if set(labels) != station_ids:
        raise ValueError("Station labels do not cover SVG anchors")
    centers = {}
    for sid in station_ids:
        pts = [p for (s, _), p in anchors.items() if s == sid]
        centers[sid] = complex(
            (min(p.real for p in pts) + max(p.real for p in pts)) / 2,
            (min(p.imag for p in pts) + max(p.imag for p in pts)) / 2,
        )
    for e in edges.values():
        e["points"] = join_station_centers(
            e.pop("curve"), centers[e["from_id"]], centers[e["to_id"]]
        )
        if not all(
            math.isfinite(v) and 0 <= v <= 4096 for point in e["points"] for v in point
        ):
            raise ValueError("Edge outside schematic bounds")
    return centers, edges, colors, labels


def snapshot(db):
    return [
        db.execute(f"SELECT * FROM {table} ORDER BY id").fetchall()
        for table in ("stations", "lines", "edges")
    ]


def import_map(svg, atlas, backup_dir):
    source_hash = hashlib.sha256(svg.read_bytes()).hexdigest()
    centers, edges, colors, labels = read_artwork(svg)
    with (
        closing(
            sqlite3.connect(atlas.resolve().as_uri() + "?mode=rw", uri=True, timeout=10)
        ) as db,
        db,
    ):
        if db.execute(
            "SELECT value FROM metadata WHERE name='gzmp_schema'"
        ).fetchone() != ("2",):
            raise ValueError("Requires existing GZMP schema 2; migrate first")
        before = snapshot(db)
        if set(centers) != {row[0] for row in before[0]} or set(colors) != {
            row[0] for row in before[1]
        }:
            raise ValueError(
                "SVG and database station/line IDs differ; resolve maintenance changes first"
            )
        topology = db.execute("SELECT id,line_id,from_id,to_id FROM edges").fetchall()
        if set(topology) != {
            (e["id"], e["line_id"], e["from_id"], e["to_id"]) for e in edges.values()
        }:
            raise ValueError("SVG and database edge topology differ")
        for sid, name in db.execute("SELECT id,name FROM stations"):
            if labels[sid][0] != name:
                raise ValueError(f"SVG station name differs from database: {sid}")
        # Build before taking the write lock; then reject any intervening edit.
        tiles = list(encode_tiles(list(edges.values())))
        if not tiles:
            raise ValueError("SVG produced no tiles")
        db.execute("BEGIN IMMEDIATE")
        if snapshot(db) != before:
            raise ValueError("Database changed during generation; retry")
        if hashlib.sha256(svg.read_bytes()).hexdigest() != source_hash:
            raise ValueError("SVG changed during generation; retry")
        backup_dir.mkdir(parents=True, exist_ok=True)
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        backup = backup_dir / f"metro-before-svg-{stamp}-{uuid.uuid4().hex[:8]}.mbtiles"
        # A second read connection sees the committed state while BEGIN IMMEDIATE
        # prevents writers from changing it until this import commits/rolls back.
        with (
            closing(
                sqlite3.connect(atlas.resolve().as_uri() + "?mode=ro", uri=True)
            ) as source,
            closing(sqlite3.connect(backup)) as target,
        ):
            source.backup(target)
        db.executemany(
            "UPDATE stations SET x=?,y=? WHERE id=?",
            [(p.real, p.imag, sid) for sid, p in centers.items()],
        )
        db.execute(
            "UPDATE stations SET transfer=(SELECT count(DISTINCT line_id)>1 FROM edges WHERE from_id=stations.id OR to_id=stations.id)"
        )
        db.executemany(
            "UPDATE lines SET color=? WHERE id=?",
            [(color, lid) for lid, color in colors.items()],
        )
        db.execute("DELETE FROM tiles")
        db.executemany("INSERT INTO tiles VALUES(?,?,?,?)", tiles)
        db.execute("DELETE FROM metadata WHERE name GLOB 'svg_label_side_*'")
        meta = [
            (f"svg_label_side_{sid}", str(side)) for sid, (_, side) in labels.items()
        ]
        meta += [
            ("svg_source_sha256", source_hash),
            (
                "svg_geometry",
                "calibrated; one center per logical station; 40px tapered joins",
            ),
            ("minzoom", "0"),
            ("maxzoom", "5"),
        ]
        db.executemany("INSERT OR REPLACE INTO metadata(name,value) VALUES(?,?)", meta)
        if db.execute("PRAGMA integrity_check").fetchone() != ("ok",):
            raise ValueError("SQLite integrity check failed")
        db.commit()
    return {
        "stations": len(centers),
        "edges": len(edges),
        "tiles": len(tiles),
        "backup": str(backup),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--svg", type=Path, default=ROOT / "data/calibration/network-labeled.svg"
    )
    parser.add_argument("--atlas", type=Path, default=ROOT / "data/metro.mbtiles")
    parser.add_argument(
        "--backup-dir", type=Path, default=ROOT / "output/svg-import-backups"
    )
    args = parser.parse_args()
    print(
        json.dumps(
            import_map(args.svg, args.atlas, args.backup_dir), ensure_ascii=False
        )
    )


if __name__ == "__main__":
    main()
