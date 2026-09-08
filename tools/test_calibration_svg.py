"""Geometry and preservation checks for the offline SVG artwork."""

import base64
import json
import re
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

from build_calibration_svg import portion
from create_svg_trial import ROOT, SVG


class CalibrationTests(unittest.TestCase):
    def test_full_network_ids_endpoints_and_source(self):
        root = ET.parse(ROOT / "data/calibration/network.svg").getroot()
        ns = {"s": SVG}
        self.assertEqual(root.attrib["viewBox"], "0 0 4096 4096")
        network = json.loads((ROOT / "data/source/network.json").read_text("utf-8"))
        edges = {e["id"]: e for e in network["edges"]}
        anchors = {
            (int(c.attrib["data-line-id"]), int(c.attrib["data-station-id"])): (
                float(c.attrib["cx"]),
                float(c.attrib["cy"]),
            )
            for c in root.findall(".//s:circle", ns)
        }
        paths = root.findall("s:g/s:path", ns)
        self.assertEqual({int(p.attrib["id"][5:]) for p in paths}, set(edges))
        self.assertEqual(len(paths), len(edges))
        self.assertEqual(
            {sid for _, sid in anchors}, {s["id"] for s in network["stations"]}
        )
        for p in paths:
            a = p.attrib
            edge = edges[int(a["id"][5:])]
            self.assertEqual(int(a["data-line-id"]), edge["line_id"])
            for key in ("from_id", "to_id"):
                self.assertEqual(int(a["data-" + key.replace("_", "-")]), edge[key])
            coords = [float(v) for v in re.findall(r"-?\d+(?:\.\d+)?", a["d"])]
            self.assertTrue(all(0 <= v <= 4096 for v in coords))
            for actual, sid in (
                (coords[:2], edge["from_id"]),
                (coords[-2:], edge["to_id"]),
            ):
                expected = anchors[(edge["line_id"], sid)]
                self.assertLess(
                    max(abs(x - y) for x, y in zip(actual, expected)), 0.002
                )
        ids = [e.attrib["id"] for e in root.iter() if "id" in e.attrib]
        self.assertEqual(len(ids), len(set(ids)))
        reference = root.find("s:g/s:image", ns)
        embedded = reference.attrib["{http://www.w3.org/1999/xlink}href"].split(",", 1)[
            1
        ]
        self.assertEqual(
            base64.b64decode(embedded), (ROOT / "data/railway.png").read_bytes()
        )

    def test_split_quadratic_keeps_the_curve(self):
        # Known parabola: x=20t, y=20t(1-t). Crop the middle half.
        data = portion([(0j, 10 + 10j, 20 + 0j)], 0.25, 0.75)
        numbers = [float(v) for v in re.findall(r"-?\d+(?:\.\d+)?", data)]
        self.assertEqual(numbers, [5.0, 3.75, 10.0, 6.25, 15.0, 3.75])
        self.assertEqual(
            portion([(0j, None, 10 + 0j)], 0, 1), "M 0.000,0.000 L 10.000,0.000"
        )

    def test_generator_protects_edited_artwork(self):
        with tempfile.TemporaryDirectory(prefix="gzmp-svg-") as directory:
            target = Path(directory) / "network.svg"
            command = [
                sys.executable,
                str(ROOT / "tools/build_calibration_svg.py"),
                "--output",
                str(target),
            ]
            subprocess.run(command, check=True, capture_output=True)
            original = target.read_bytes()
            result = subprocess.run(command, capture_output=True, check=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(target.read_bytes(), original)
            edited = original + b"\n<!-- manual edit -->\n"
            target.write_bytes(edited)
            result = subprocess.run(
                command + ["--replace-generated"], capture_output=True, check=False
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(target.read_bytes(), edited)


if __name__ == "__main__":
    unittest.main()
