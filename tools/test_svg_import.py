"""SVG import: parser bounds, preserved maintenance data, atomic rollback."""

import sqlite3
import tempfile
import unittest
from contextlib import closing, contextmanager
from pathlib import Path
from unittest.mock import patch

from create_svg_trial import ROOT
from import_svg_map import import_map, read_artwork
from svg_geometry import flatten_path, join_station_centers


@contextmanager
def connection(path):
    with closing(sqlite3.connect(path)) as db, db:
        yield db


class SvgImportTests(unittest.TestCase):
    def test_curves_and_invalid_commands(self):
        curve = flatten_path("m 0,0 q 10,20 20,0 t 20,0 c 1,2 3,4 5,6 l 1,0 h 1 v 1")
        self.assertEqual(curve[0], 0j)
        self.assertEqual(curve[-1], 47 + 7j)
        self.assertGreater(len(curve), 10)
        for data in (
            "M 1,2",
            "M 0,0 L 1",
            "L 1,2",
            "M 0,0 A 1,2 3,4 5,6 7",
            "M 0,0 L 1e999,2",
            "M 0,0 M 1,1 L 2,2",
            "M 0,0 L 0,0",
        ):
            with self.assertRaises(ValueError, msg=data):
                flatten_path(data)
        joined = join_station_centers([0j, 100 + 0j], 0 + 10j, 100 + 10j)
        self.assertEqual(joined[0], [0, 10])
        self.assertEqual(joined[-1], [100, 10])
        self.assertTrue(any(x == 48 and y == 0 for x, y in joined))

    def test_logical_stations_share_exact_edge_endpoints(self):
        centers, edges, colors, labels = read_artwork(
            ROOT / "data/calibration/network-labeled.svg"
        )
        self.assertEqual(
            (len(centers), len(edges), len(colors), len(labels)), (357, 421, 21, 357)
        )
        for e in edges.values():
            self.assertEqual(complex(*e["points"][0]), centers[e["from_id"]])
            self.assertEqual(complex(*e["points"][-1]), centers[e["to_id"]])

    def test_transaction_preserves_weights_and_rolls_back(self):
        with tempfile.TemporaryDirectory(prefix="gzmp-import-") as directory:
            path = Path(directory) / "map.mbtiles"
            with (
                connection(ROOT / "metro.mbtiles") as source,
                connection(path) as target,
            ):
                source.backup(target)
            with connection(path) as db:
                db.execute("UPDATE edges SET meters=1234,seconds=77 WHERE id=1")
                db.execute(
                    "UPDATE stations SET en_name='Preserve this edit' WHERE id=1"
                )
            report = import_map(
                ROOT / "data/calibration/network-labeled.svg",
                path,
                Path(directory) / "backups",
            )
            self.assertGreater(report["tiles"], 100)
            with connection(path) as db, connection(report["backup"]) as backup:
                self.assertEqual(
                    db.execute(
                        "SELECT meters,seconds FROM edges WHERE id=1"
                    ).fetchone(),
                    (1234, 77),
                )
                self.assertEqual(
                    db.execute("SELECT en_name FROM stations WHERE id=1").fetchone(),
                    ("Preserve this edit",),
                )
                self.assertEqual(
                    backup.execute(
                        "SELECT meters,seconds FROM edges WHERE id=1"
                    ).fetchone(),
                    (1234, 77),
                )
                # Make a later write fail after coordinate/color updates.
                db.execute(
                    "CREATE TRIGGER fail_import BEFORE DELETE ON tiles BEGIN SELECT RAISE(ABORT,'forced failure'); END"
                )
                before = list(db.iterdump())
                tiles = db.execute("SELECT * FROM tiles LIMIT 1").fetchall()
            with (
                patch("import_svg_map.encode_tiles", return_value=iter(tiles)),
                self.assertRaises(sqlite3.IntegrityError),
            ):
                import_map(
                    ROOT / "data/calibration/network-labeled.svg",
                    path,
                    Path(directory) / "backups",
                )
            with connection(path) as db:
                self.assertEqual(list(db.iterdump()), before)


if __name__ == "__main__":
    unittest.main()
