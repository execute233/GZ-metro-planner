"""Render SVG proof images; requires requirements-calibration.txt."""

import argparse
import copy
import io
import re
import xml.etree.ElementTree as ET
from pathlib import Path

import resvg_py
from create_svg_trial import ROOT, SVG
from PIL import Image


def render(root):
    return Image.open(
        io.BytesIO(
            resvg_py.svg_to_bytes(svg_string=ET.tostring(root, encoding="unicode"))
        )
    ).convert("RGB")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--svg", type=Path, default=ROOT / "data/calibration/network.svg"
    )
    parser.add_argument("--output", type=Path, default=ROOT / "output/svg-calibration")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    root = ET.parse(args.svg).getroot()
    ns = {"s": SVG}
    # Magenta over the original is deliberately a proofreading color.
    overlay = copy.deepcopy(root)
    overlay.find("s:g[@id='station-anchors']", ns).set("style", "display:none")
    for g in overlay.findall("s:g", ns):
        if g.attrib.get("id", "").startswith("line-"):
            g.set(
                "style",
                "fill:none;stroke:#ff00cb;stroke-width:2;stroke-linecap:round;stroke-linejoin:round",
            )
    proof = render(overlay)
    proof.resize((2048, 2048)).save(args.output / "network-overlay.png")
    boxes = {
        "central": (780, 1900, 2380, 2790),
        "north": (350, 500, 2990, 1950),
        "east": (2200, 900, 3950, 2880),
        "foshan": (50, 1950, 1900, 3670),
        "south": (950, 2750, 3600, 3700),
    }
    for name, box in boxes.items():
        proof.crop(box).save(args.output / f"overlay-{name}.png")
    clean = copy.deepcopy(root)
    clean.find("s:g[@id='reference']", ns).set("style", "display:none")
    clean.find("s:g[@id='station-anchors']", ns).set("style", "display:none")
    # Explicit white backdrop for a portable RGB PNG.
    background = ET.Element(
        f"{{{SVG}}}rect", {"width": "4096", "height": "4096", "fill": "white"}
    )
    clean.insert(0, background)
    render(clean).resize((2048, 2048)).save(args.output / "network-vector.png")
    for group in root.findall("s:g", ns):
        key = group.attrib.get("id", "")
        if not key.startswith("line-"):
            continue
        individual = copy.deepcopy(overlay)
        for g in individual.findall("s:g", ns):
            other = g.attrib.get("id", "")
            if other.startswith("line-") and other != key:
                g.set("style", "display:none")
        numbers = []
        for path in group.findall("s:path", ns):
            numbers.extend(
                float(x) for x in re.findall(r"-?\d+(?:\.\d+)?", path.attrib["d"])
            )
        xs, ys = numbers[::2], numbers[1::2]
        box = (
            max(0, int(min(xs)) - 65),
            max(0, int(min(ys)) - 65),
            min(4096, int(max(xs)) + 90),
            min(4096, int(max(ys)) + 65),
        )
        render(individual).crop(box).save(args.output / f"{key}.png")
    print(args.output)


if __name__ == "__main__":
    main()
