"""Verify the delivered label layout independently from candidate selection."""

import copy
import io
import json
import math
import unittest
import xml.etree.ElementTree as ET

import numpy as np
import resvg_py
from create_svg_trial import ROOT, SVG
from layout_svg_labels import NS, ink_in_box, intersects, obstacles
from PIL import Image


class LabelLayoutTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = ET.parse(ROOT / "data/calibration/network.svg").getroot()
        cls.result = ET.parse(ROOT / "data/calibration/network-labeled.svg").getroot()
        cls.report = json.loads(
            (ROOT / "data/calibration/network-labeled.report.json").read_text("utf-8")
        )

    def test_names_and_calibrated_geometry_are_preserved(self):
        def names(root):
            return {
                e.attrib["id"]: e.text
                for e in root.findall("s:g[@id='station-labels']/s:text", NS)
            }

        self.assertEqual(names(self.source), names(self.result))
        self.assertEqual(len(names(self.result)), 357)
        for query in ("s:g/s:path", ".//s:circle"):
            before = {e.attrib["id"]: e.attrib for e in self.source.findall(query, NS)}
            after = {e.attrib["id"]: e.attrib for e in self.result.findall(query, NS)}
            for key, attrs in before.items():
                self.assertEqual(attrs, after[key])

    def test_clearance_from_tracks_stations_and_other_labels(self):
        _, integral = obstacles(self.source)
        labels = self.report["labels"]
        self.assertEqual(len({x["station_id"] for x in labels}), 357)
        for i, item in enumerate(labels):
            box = item["box"]
            self.assertTrue(0 <= box[0] < box[2] <= 4096)
            self.assertTrue(0 <= box[1] < box[3] <= 4096)
            self.assertEqual(ink_in_box(integral, box), 0, item["name"])
            self.assertEqual(item["leader_track_pixels"], 0, item["name"])
            for other in labels[i + 1 :]:
                self.assertFalse(
                    intersects(box, other["box"]), (item["name"], other["name"])
                )

    def test_actual_rendered_glyphs_fit_reserved_rectangles(self):
        root = ET.Element(f"{{{SVG}}}svg", self.result.attrib)
        ET.SubElement(root, f"{{{SVG}}}rect", width="4096", height="4096", fill="white")
        root.append(copy.deepcopy(self.result.find("s:g[@id='station-labels']", NS)))
        png = resvg_py.svg_to_bytes(
            svg_string=ET.tostring(root, encoding="unicode"),
            font_files=[self.report["font"], self.report["bold_font"]],
        )
        ink = np.asarray(Image.open(io.BytesIO(png)).convert("L")) < 220
        reserved = np.zeros((4096, 4096), dtype=bool)
        for item in self.report["labels"]:
            x0, y0, x1, y1 = item["box"]
            reserved[math.floor(y0) : math.ceil(y1), math.floor(x0) : math.ceil(x1)] = (
                True
            )
        self.assertGreater(int(ink.sum()), 10000)
        self.assertEqual(int((ink & ~reserved).sum()), 0)


if __name__ == "__main__":
    unittest.main()
