"""Deterministic QA preview of generated geometry or the C-exported cell frame."""

import json
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

root = Path(__file__).resolve().parents[1]
if len(sys.argv) > 1:
    p = Path(sys.argv[1])
    d = json.loads(p.read_text(encoding="utf8"))
    cw, ch = 12, 24
    im = Image.new("RGB", (d["width"] * cw, d["height"] * ch), "#101318")
    draw = ImageDraw.Draw(im)
    font = ImageFont.truetype("C:/Windows/Fonts/msyh.ttc", 18)
    for i, (glyph, color, dim, cont) in enumerate(d["cells"]):
        if cont:
            continue
        x = i % d["width"] * cw
        y = i // d["width"] * ch
        rgb = (
            d["colors"][color - 4]
            if color >= 5
            else 0x6CDEEB
            if color == 1
            else 0xDCE0E5
        )
        c = tuple(((rgb >> shift) & 255) // (2 if dim else 1) for shift in (16, 8, 0))
        if 0x2800 <= ord(glyph) <= 0x28FF:
            bits = ((1, 8), (2, 16), (4, 32), (64, 128))
            mask = ord(glyph) - 0x2800
            for row in range(4):
                for col in range(2):
                    if mask & bits[row][col]:
                        draw.ellipse(
                            (
                                x + col * 6 + 1,
                                y + row * 6 + 1,
                                x + col * 6 + 3,
                                y + row * 6 + 3,
                            ),
                            fill=c,
                        )
        else:
            draw.text((x, y), glyph, font=font, fill=c)
    im.save(p.with_suffix(".png"))
else:
    d = json.loads((root / "data/source/network.json").read_text(encoding="utf8"))
    im = Image.new("RGB", (1600, 1600), "#101318")
    draw = ImageDraw.Draw(im)
    colors = {l["id"]: "#" + l["color"] for l in d["lines"]}
    for e in d["edges"]:
        draw.line(
            [(x * 1600 / 4096, y * 1600 / 4096) for x, y in e["points"]],
            fill=colors[e["line_id"]],
            width=2,
        )
    for s in d["stations"]:
        x, y = s["x"] * 1600 / 4096, s["y"] * 1600 / 4096
        draw.ellipse((x - 2, y - 2, x + 2, y + 2), fill="#eeeeee")
    im.save(root / "cmake-build-tools/geometry.png")
