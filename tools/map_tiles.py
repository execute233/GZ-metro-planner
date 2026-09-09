"""Shared gzip-MVT encoder, virtual 4096px square, XYZ input / TMS storage."""

import gzip

import mapbox_vector_tile
from shapely.geometry import LineString, box, mapping


def encode_tiles(edges):
    geoms = [(e, LineString(e["points"])) for e in edges]
    for z in range(6):
        n = 2**z
        size = 4096 / n
        for y in range(n):
            for x in range(n):
                bounds = (x * size, y * size, (x + 1) * size, (y + 1) * size)
                region = box(*bounds)
                features = []
                for e, g in geoms:
                    if not g.intersects(region):
                        continue
                    clipped = g.intersection(region)
                    if (
                        clipped.geom_type not in ("LineString", "MultiLineString")
                        or clipped.is_empty
                    ):
                        continue
                    features.append(
                        {
                            "id": e["id"],
                            "geometry": mapping(clipped),
                            "properties": {"line_id": e["line_id"]},
                        }
                    )
                if not features:
                    continue
                tile = mapbox_vector_tile.encode(
                    {"name": "edges", "features": features},
                    default_options={
                        "quantize_bounds": bounds,
                        "extents": 4096,
                        "y_coord_down": True,
                    },
                )
                decoded = mapbox_vector_tile.decode(
                    tile, default_options={"y_coord_down": True}
                )
                if decoded["edges"]["extent"] != 4096:
                    raise ValueError("Invalid emitted MVT extent")
                yield z, x, n - 1 - y, gzip.compress(tile, mtime=0)
