"""Match independently sourced station sequences to OCR of the supplied map.
Produces an editable draft plus an explicit audit; no runtime dependency.
"""

import argparse
import heapq
import json
import math
import re
from collections import defaultdict
from itertools import pairwise
from pathlib import Path

import cv2
import numpy as np
from pypinyin import Style, lazy_pinyin

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument(
    "--force",
    action="store_true",
    help="replace the editable network, discarding manual changes",
)
args = parser.parse_args()
if (ROOT / "data/source/network.json").exists() and not args.force:
    parser.error(
        "network.json already exists; use --force only to intentionally re-extract it"
    )


def read(name):
    return json.loads((ROOT / "data/source" / name).read_text(encoding="utf8"))


def clean(s):
    s = re.sub(r"\([^)]*\)|（[^）]*）", "", s)
    return "".join(c for c in s if "\u4e00" <= c <= "\u9fff").replace("塱", "朗")


raw = [l for l in read("amap-guangzhou.json")["l"] if l["ln"] != "APM线"]
for l in raw:
    l["cl"] = {
        "1号线": "F5D546",
        "3号线": "ECA154",
        "11号线": "F2B700",
        "12号线": "50580D",
    }.get(l["ln"], l["cl"])
for l in read("amap-foshan.json")["l"]:
    if l["ln"] == "3号线":
        l["ln"] = "佛山3号线"
        raw.append(l)
    # Tram routes and APM are not drawn on this particular supplied image.

ocr = read("ocr.json")
positions = defaultdict(list)
for o in ocr:
    text = clean(o["text"])
    b = np.array(o["box"])
    p = b.mean(axis=0)
    if text:
        positions[text].append((float(p[0]), float(p[1]), float(o["score"])))
names = {clean(s["n"]): s["n"] for l in raw for s in l["st"]}
found = {}
missing = []
for key, name in names.items():
    candidates = positions.get(key, [])
    if not candidates:
        candidates = [
            p
            for text, ps in positions.items()
            if len(key) >= 3 and key in text
            for p in ps
        ]
    if candidates:
        # Overlapping OCR crops produce duplicates; select highest confidence.
        found[key] = max(candidates, key=lambda a: a[2])[:2]
    else:
        missing.append(name)

# These two stations bridge the deliberate gap in the supplied Foshan diagram.
# Do not invent a connection absent from the image.
names = {k: n for k, n in names.items() if n not in ("敦厚", "佛山站")}

overrides = ROOT / "data/source/station_overrides.json"
if overrides.exists():
    for name, p in json.loads(overrides.read_text(encoding="utf8")).items():
        found[clean(name)] = p
missing = [name for key, name in names.items() if key not in found]
(ROOT / "data/source/missing_stations.json").write_text(
    json.dumps(missing, ensure_ascii=False, indent=2), encoding="utf8"
)
print(
    "Matched",
    len(found),
    "Missing",
    len(missing),
    json.dumps(missing, ensure_ascii=False),
    flush=True,
)

# Draft only: no invented coordinates for unmatched stations.
stations = []
index = {}
for key, name in names.items():
    if key not in found:
        continue
    name = re.sub(r"\([^)]*\)|（[^）]*）", "", name)
    index[key] = len(stations) + 1
    stations.append(
        {
            "id": index[key],
            "name": name,
            "pinyin": "".join(lazy_pinyin(name)),
            "initials": "".join(lazy_pinyin(name, style=Style.FIRST_LETTER)),
            "x": found[key][0],
            "y": found[key][1],
        }
    )

image = cv2.imread(str(ROOT / "data/railway.png"))
small = cv2.resize(image, (1024, 1024))
hsv = cv2.cvtColor(small, cv2.COLOR_BGR2HSV)
lines = []
edges = []
lineids = {}
skipped = []
for l in raw:
    name = l["ln"].replace("14号线支线(知识城线)", "14号线支线")
    if name not in lineids:
        lineids[name] = len(lines) + 1
        lines.append({"id": lineids[name], "name": name, "color": l["cl"]})
    lid = lineids[name]
    rgb = l["cl"]
    bgr = np.uint8([[[int(rgb[4:6], 16), int(rgb[2:4], 16), int(rgb[:2], 16)]]])
    target = int(cv2.cvtColor(bgr, cv2.COLOR_BGR2HSV)[0, 0, 0])
    hue = np.abs(hsv[:, :, 0].astype(int) - target)
    hue = np.minimum(hue, 180 - hue)
    target_sat = int(cv2.cvtColor(bgr, cv2.COLOR_BGR2HSV)[0, 0, 1])
    mask = (
        (hue < 7)
        & (np.abs(hsv[:, :, 1].astype(int) - target_sat) < 55)
        & (hsv[:, :, 1] > 75)
        & (hsv[:, :, 2] > 50)
    ).astype(np.uint8)
    dist = cv2.distanceTransform(1 - mask, cv2.DIST_L2, 3)

    def snap(p, dist, r=30):
        x, y = round(p[0] / 4), round(p[1] / 4)
        x0, y0 = max(0, x - r), max(0, y - r)
        x1, y1 = min(1024, x + r + 1), min(1024, y + r + 1)
        yy, xx = np.mgrid[y0:y1, x0:x1]
        cost = (xx - x) ** 2 + (yy - y) ** 2 + dist[y0:y1, x0:x1] ** 2 * 40
        j, i = np.unravel_index(np.argmin(cost), cost.shape)
        return (int(i + x0), int(j + y0))

    def path(a, b, dist, radius=30):
        a = snap(a, dist, radius)
        b = snap(b, dist, radius)
        todo = [(0, 0, a)]
        costs = {a: 0}
        prev = {}
        visits = 0
        while todo:
            _, g, u = heapq.heappop(todo)
            if g != costs[u]:
                continue
            if u == b:
                break
            visits += 1
            if visits > 100000:
                return None
            for dx, dy in (
                (1, 0),
                (-1, 0),
                (0, 1),
                (0, -1),
                (1, 1),
                (1, -1),
                (-1, 1),
                (-1, -1),
            ):
                v = (u[0] + dx, u[1] + dy)
                if not (0 <= v[0] < 1024 and 0 <= v[1] < 1024):
                    continue
                ng = g + (1.414 if dx and dy else 1) * (
                    1 + float(dist[v[1], v[0]]) ** 2 * 5
                )
                if ng < costs.get(v, float("inf")):
                    costs[v] = ng
                    prev[v] = u
                    heapq.heappush(todo, (ng + math.dist(v, b), ng, v))
        if b not in costs:
            return None
        pts = [b]
        u = b
        while u != a:
            u = prev[u]
            pts.append(u)
        pts.reverse()
        simple = cv2.approxPolyDP(
            np.array(pts, dtype=np.float32).reshape(-1, 1, 2), 1.0, False
        )
        return [[float(p[0][0] * 4), float(p[0][1] * 4)] for p in simple]

    seq = l["st"][:]
    if name == "11号线":
        seq = seq + [seq[0]]
    for a, b in pairwise(seq):
        ka, kb = clean(a["n"]), clean(b["n"])
        if ka not in index or kb not in index:
            skipped.append([name, a["n"], b["n"]])
            continue
        points = path(found[ka], found[kb], dist)
        if not points or len(points) < 2:
            strict_dist = dist
            broad = ((hue < 9) & (hsv[:, :, 1] > 75) & (hsv[:, :, 2] > 50)).astype(
                np.uint8
            )
            dist = cv2.distanceTransform(1 - broad, cv2.DIST_L2, 3)
            points = path(found[ka], found[kb], dist, 18)
            dist = strict_dist
        if not points or len(points) < 2:
            skipped.append([name, a["n"], b["n"], "trace failed"])
            continue
        edges.append(
            {
                "id": len(edges) + 1,
                "line_id": lid,
                "from_id": index[ka],
                "to_id": index[kb],
                "seconds": 60,
                "meters": 1000,
                "points": points,
            }
        )
    print(name, len(edges), flush=True)
members = defaultdict(set)
for e in edges:
    for sid in (e["from_id"], e["to_id"]):
        members[sid].add(e["line_id"])
for s in stations:
    s["transfer"] = len(members[s["id"]]) > 1
    ends = [
        e["points"][0 if e["from_id"] == s["id"] else -1]
        for e in edges
        if s["id"] in (e["from_id"], e["to_id"])
    ]
    if ends:
        s["x"], s["y"] = np.median(ends, axis=0).tolist()
for e in edges:
    a, b = stations[e["from_id"] - 1], stations[e["to_id"] - 1]
    e["points"][0] = [a["x"], a["y"]]
    e["points"][-1] = [b["x"], b["y"]]
if missing or any(len(row) > 3 for row in skipped):
    raise RuntimeError(
        "Unresolved stations or failed traces; refusing to replace network.json"
    )
doc = {
    "schema": 1,
    "source_image": "railway.png",
    "status": "draft; requires visual/topology verification",
    "stations": stations,
    "lines": lines,
    "edges": edges,
    "missing_stations": missing,
    "skipped_edges": skipped,
}
(ROOT / "data/source/network.json").write_text(
    json.dumps(doc, ensure_ascii=False, indent=2), encoding="utf8"
)
print(
    "Generated",
    len(stations),
    len(lines),
    len(edges),
    "Skipped",
    len(skipped),
    flush=True,
)
