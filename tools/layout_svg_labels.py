"""Place editable Chinese station labels on the calibrated SVG (offline only)."""

import argparse
import copy
import hashlib
import io
import json
import math
import xml.etree.ElementTree as ET
from pathlib import Path

import numpy as np
import resvg_py
from create_svg_trial import INK, ROOT, SVG, element, layer
from PIL import Image, ImageFont

NS = {"s": SVG}


def intersects(a, b):
    return a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]


def nearest(box, p):
    return max(box[0], min(box[2], p[0])), max(box[1], min(box[3], p[1]))


def distance(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


def obstacles(root):
    """Rasterize current SVG curves; no regeneration of calibrated geometry."""
    mask = ET.Element(f"{{{SVG}}}svg", root.attrib)
    element(mask, "rect", width="4096", height="4096", fill="white")
    for group in root.findall("s:g", NS):
        if group.attrib.get("id", "").startswith("line-"):
            for path in group.findall("s:path", NS):
                p = copy.deepcopy(path)
                p.set(
                    "style",
                    "fill:none;stroke:black;stroke-width:7;stroke-linecap:round;stroke-linejoin:round",
                )
                mask.append(p)
    for circle in root.findall(".//s:circle", NS):
        c = copy.deepcopy(circle)
        c.set("r", "8")
        c.set("style", "fill:black;stroke:none")
        mask.append(c)
    png = resvg_py.svg_to_bytes(svg_string=ET.tostring(mask, encoding="unicode"))
    pixels = np.asarray(Image.open(io.BytesIO(png)).convert("L")) < 250
    integral = np.pad(
        pixels.astype(np.int32).cumsum(0, dtype=np.int32).cumsum(1, dtype=np.int32),
        ((1, 0), (1, 0)),
    )
    return pixels, integral


def ink_in_box(integral, box):
    x0, y0 = max(0, math.floor(box[0])), max(0, math.floor(box[1]))
    x1, y1 = min(4096, math.ceil(box[2])), min(4096, math.ceil(box[3]))
    return int(
        integral[y1, x1] - integral[y1, x0] - integral[y0, x1] + integral[y0, x0]
    )


def candidate_boxes(bounds, width, height):
    left, top, right, bottom = bounds
    cx, cy = (left + right) / 2, (top + bottom) / 2
    seen = set()
    for gap in (9, 15, 23, 35, 49, 69, 97):
        positions = [
            (right + gap, cy - height / 2, "E"),
            (left - gap - width, cy - height / 2, "W"),
            (cx - width / 2, top - gap - height, "N"),
            (cx - width / 2, bottom + gap, "S"),
            (right + gap, top - gap - height, "NE"),
            (left - gap - width, top - gap - height, "NW"),
            (right + gap, bottom + gap, "SE"),
            (left - gap - width, bottom + gap, "SW"),
        ]
        for x, y, side in positions:
            shifts = [(0, 0)]
            if side in ("N", "S"):
                shifts += [(width * f, 0) for f in (-0.5, -0.25, 0.25, 0.5)]
            elif side in ("E", "W"):
                shifts += [(0, height * f) for f in (-2, -1, 1, 2)]
            for dx, dy in shifts:
                box = (
                    round(x + dx, 2),
                    round(y + dy, 2),
                    round(x + dx + width, 2),
                    round(y + dy + height, 2),
                )
                if box in seen or min(box) < 3 or box[2] > 4093 or box[3] > 4093:
                    continue
                seen.add(box)
                yield box, side


def leader_for(box, anchors):
    return min(
        ((p, nearest(box, p)) for p in anchors), key=lambda pair: distance(*pair)
    )


def leader_crossings(pair, pixels):
    a, b = pair
    length = distance(a, b)
    count = 0
    for d in range(10, math.ceil(length)):
        x, y = (round(a[i] + d / length * (b[i] - a[i])) for i in (0, 1))
        count += int(pixels[y, x])
    return count


def arrange(root, regular, bold, overrides=None):
    overrides = overrides or {}
    pixels, integral = obstacles(root)
    anchors = {}
    for circle in root.findall(".//s:circle", NS):
        sid = int(circle.attrib["data-station-id"])
        anchors.setdefault(sid, []).append(
            (float(circle.attrib["cx"]), float(circle.attrib["cy"]))
        )
    labels = root.find("s:g[@id='station-labels']", NS)
    items = []
    for text in labels.findall("s:text", NS):
        sid = int(text.attrib["data-station-id"])
        points = anchors[sid]
        transfer = len(points) > 1
        size = 17 if transfer else 16
        font = ImageFont.truetype(str(bold if transfer else regular), size)
        glyph = font.getbbox(text.text, anchor="ls")
        # The padded rectangle includes the white halo and rasterization slack.
        width, height = glyph[2] - glyph[0] + 6, glyph[3] - glyph[1] + 6
        bounds = (
            min(p[0] for p in points),
            min(p[1] for p in points),
            max(p[0] for p in points),
            max(p[1] for p in points),
        )
        options = []
        for box, side in candidate_boxes(bounds, width, height):
            if ink_in_box(integral, box):
                continue
            pair = leader_for(box, points)
            gap = distance(*pair)
            center = ((box[0] + box[2]) / 2, (box[1] + box[3]) / 2)
            cost = gap + 0.12 * min(distance(p, center) for p in points)
            preference = overrides.get(str(sid), {}).get("preferred_side")
            if preference and side != preference:
                cost += 100
            if gap > 24:
                cost += 20 + 8 * leader_crossings(pair, pixels)
            options.append((cost, box, side, pair, gap))
        options.sort()
        if not options:
            raise ValueError(f"No track-free label candidate: {sid} {text.text}")
        items.append(
            {
                "id": sid,
                "name": text.text,
                "transfer": transfer,
                "size": size,
                "glyph": glyph,
                "element": text,
                "options": options,
                "anchors": points,
            }
        )
    # Most constrained transfers get first choice, then constrained ordinary stops.
    items.sort(
        key=lambda item: (
            not item["transfer"],
            len(item["options"]),
            -len(item["name"]),
            item["id"],
        )
    )
    placed = []
    for item in items:
        selected = next(
            (
                o
                for o in item["options"]
                if not any(intersects(o[1], p["box"]) for p in placed)
            ),
            None,
        )
        if selected is None:
            raise ValueError(
                f"No non-overlapping placement: {item['id']} {item['name']}"
            )
        _, box, side, pair, gap = selected
        item.update(box=box, side=side, pair=pair, gap=gap)
        placed.append(item)
    return placed, pixels


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--input", type=Path, default=ROOT / "data/calibration/network.svg"
    )
    parser.add_argument(
        "--output", type=Path, default=ROOT / "data/calibration/network-labeled.svg"
    )
    parser.add_argument("--font", type=Path, default=Path("C:/Windows/Fonts/msyh.ttc"))
    parser.add_argument(
        "--bold-font", type=Path, default=Path("C:/Windows/Fonts/msyhbd.ttc")
    )
    parser.add_argument(
        "--overrides", type=Path, default=ROOT / "data/calibration/label_overrides.json"
    )
    parser.add_argument("--replace-generated", action="store_true")
    args = parser.parse_args()
    if args.input.resolve() == args.output.resolve():
        raise SystemExit("Use a separate output to preserve the calibration master")
    report_path = args.output.with_suffix(".report.json")
    if args.output.exists():
        if not args.replace_generated or not report_path.exists():
            raise SystemExit("Output exists; refusing to overwrite artwork")
        old = json.loads(report_path.read_text("utf-8"))
        if (
            old.get("svg_sha256")
            != hashlib.sha256(args.output.read_bytes()).hexdigest()
        ):
            raise SystemExit("Edited artwork detected; refusing to overwrite")
    root = ET.parse(args.input).getroot()
    if root.attrib.get("viewBox") != "0 0 4096 4096" or any(
        "transform" in e.attrib for e in root.iter()
    ):
        raise SystemExit("Expected 4096px SVG without unflattened transforms")
    overrides = json.loads(args.overrides.read_text("utf-8"))
    label_ids = {
        e.attrib["data-station-id"]
        for e in root.findall("s:g[@id='station-labels']/s:text", NS)
    }
    if not set(overrides) <= label_ids or any(
        v.get("preferred_side") not in {"N", "S", "E", "W", "NE", "NW", "SE", "SW"}
        for v in overrides.values()
    ):
        raise SystemExit("Invalid label override station ID or side")
    family = ImageFont.truetype(str(args.font), 16).getname()[0]
    if ImageFont.truetype(str(args.bold_font), 17).getname()[0] != family:
        raise SystemExit("Regular and bold fonts must belong to the same family")
    placed, pixels = arrange(root, args.font, args.bold_font, overrides)
    root.find("s:g[@id='reference']", NS).set("style", "display:none")
    paper = layer(root, "label-paper", "00 白色背景（可隐藏）")
    element(paper, "rect", width="4096", height="4096", fill="white")
    root.remove(paper)
    root.insert(0, paper)
    labels = root.find("s:g[@id='station-labels']", NS)
    labels.set(f"{{{INK}}}label", "91 中文站名（已排版）")
    labels.set(
        "style",
        "fill:#202020;stroke:white;stroke-width:2;paint-order:stroke fill;stroke-linejoin:round",
    )
    labels.set("font-family", family)
    leaders = layer(root, "label-leaders", "89 标签引线")
    leaders.set("style", "fill:none;stroke:#737373;stroke-width:1")
    root.remove(leaders)
    root.insert(list(root).index(root.find("s:g[@id='station-anchors']", NS)), leaders)
    report = {
        "input_sha256": hashlib.sha256(args.input.read_bytes()).hexdigest(),
        "font": str(args.font),
        "bold_font": str(args.bold_font),
        "overrides": overrides,
        "labels": [],
    }
    for item in placed:
        box, glyph, text = item["box"], item["glyph"], item["element"]
        text.set("x", f"{box[0] + 3 - glyph[0]:.2f}")
        text.set("y", f"{box[1] + 3 - glyph[1]:.2f}")
        text.set("font-size", str(item["size"]))
        text.set("font-weight", "bold" if item["transfer"] else "normal")
        text.set("data-placement", item["side"])
        text.set("data-label-box", " ".join(map(str, box)))
        leader = item["gap"] > 24
        if leader:
            a, b = item["pair"]
            length = distance(a, b)
            start = [a[i] + 5 / length * (b[i] - a[i]) for i in (0, 1)]
            element(
                leaders,
                "path",
                id=f"leader-{item['id']}",
                d=f"M {start[0]:.2f},{start[1]:.2f} L {b[0]:.2f},{b[1]:.2f}",
            )
        report["labels"].append(
            {
                "station_id": item["id"],
                "name": item["name"],
                "transfer": item["transfer"],
                "box": box,
                "side": item["side"],
                "gap": round(item["gap"], 2),
                "leader": leader,
                "leader_track_pixels": leader_crossings(item["pair"], pixels)
                if leader
                else 0,
            }
        )
    report["label_count"] = len(placed)
    report["leader_count"] = sum(x["leader"] for x in report["labels"])
    ET.indent(root)
    payload = ET.tostring(root, encoding="utf-8", xml_declaration=True)
    report["svg_sha256"] = hashlib.sha256(payload).hexdigest()
    args.output.write_bytes(payload)
    report_path.write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n", "utf-8"
    )
    out = ROOT / "output/svg-labels"
    out.mkdir(parents=True, exist_ok=True)
    png = resvg_py.svg_to_bytes(
        svg_string=ET.tostring(root, encoding="unicode"),
        font_files=[str(args.font), str(args.bold_font)],
    )
    image = Image.open(io.BytesIO(png))
    image.save(out / "network-labeled.png")
    image.crop((750, 1900, 2380, 2820)).save(out / "central-labels.png")
    image.crop((50, 2700, 1900, 3670)).save(out / "foshan-labels.png")
    print(f"{len(placed)} labels, {report['leader_count']} leaders: {args.output}")


if __name__ == "__main__":
    main()
