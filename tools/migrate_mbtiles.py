"""Upgrade an existing GZMP SQLite atlas for in-map maintenance, preserving tiles.

Usage: python tools/migrate_mbtiles.py [map.mbtiles]
Close the application before migrating. No source JSON is read.
"""
import sqlite3
import sys
from pathlib import Path

path = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1] / "data/metro.mbtiles"
with sqlite3.connect(path.resolve().as_uri() + "?mode=rw", uri=True) as db:
    db.execute("BEGIN IMMEDIATE")
    version = db.execute("SELECT value FROM metadata WHERE name='gzmp_schema'").fetchone()
    if version == ("1",):
        db.execute("ALTER TABLE stations ADD COLUMN en_name TEXT NOT NULL DEFAULT ''")
        db.execute("ALTER TABLE lines ADD COLUMN en_name TEXT NOT NULL DEFAULT ''")
        db.execute("ALTER TABLE lines ADD COLUMN ansi_color INTEGER NOT NULL DEFAULT 37")
        db.execute("UPDATE metadata SET value='2' WHERE name='gzmp_schema'")
    elif version != ("2",):
        raise ValueError("Unsupported GZMP schema")
    if db.execute("PRAGMA integrity_check").fetchone() != ("ok",):
        raise ValueError("Database integrity check failed")
print(f"{path}: schema 2")
