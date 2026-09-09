"""Bounded SVG centerline reader for the calibration's editable M/L/Q/C paths."""

import math
import re
from itertools import pairwise

NUMBER = r"[-+]?(?:\d*\.\d+|\d+\.?\d*)(?:[eE][-+]?\d+)?"
TOKEN = re.compile(f"{NUMBER}|[A-Za-z]")


def flatten_path(data, tolerance=0.35):
    if len(data) > 1_000_000 or re.sub(TOKEN, "", data).strip(" ,\t\r\n"):
        raise ValueError("Invalid or oversized SVG path")
    tokens = TOKEN.findall(data)
    result = []
    cursor = 0j
    previous = None
    control = None
    command = None
    index = 0

    def append(p):
        if not (math.isfinite(p.real) and math.isfinite(p.imag)) or abs(p) > 100000:
            raise ValueError("Nonfinite or excessive SVG coordinate")
        if not result or abs(result[-1] - p) > 1e-8:
            result.append(p)
        if len(result) > 100000:
            raise ValueError("SVG path too complex")

    def curve(points, depth=0):
        # Distance to the chord segment (not infinite line) also detects loops.
        a, b = points[0], points[-1]
        delta = b - a

        def deviation(p):
            t = (
                0
                if not abs(delta)
                else max(
                    0, min(1, ((p - a).conjugate() * delta).real / abs(delta) ** 2)
                )
            )
            return abs(p - (a + t * delta))

        if max(deviation(p) for p in points[1:-1]) <= tolerance:
            append(b)
            return
        if depth >= 20:
            raise ValueError("SVG curve subdivision limit")
        levels = [points]
        while len(levels[-1]) > 1:
            last = levels[-1]
            levels.append([(a + b) / 2 for a, b in pairwise(last)])
        curve([row[0] for row in levels], depth + 1)
        curve([row[-1] for row in reversed(levels)], depth + 1)

    while index < len(tokens):
        if tokens[index].isalpha():
            command = tokens[index]
            index += 1
        if command is None or command.upper() not in {
            "M",
            "L",
            "H",
            "V",
            "Q",
            "C",
            "S",
            "T",
        }:
            raise ValueError(
                "Unsupported SVG path command; convert arcs/closed subpaths to open curves"
            )
        kind = command.upper()
        count = {"M": 2, "L": 2, "H": 1, "V": 1, "Q": 4, "C": 6, "S": 4, "T": 2}[kind]
        if index + count > len(tokens) or any(
            t.isalpha() for t in tokens[index : index + count]
        ):
            raise ValueError("Truncated SVG path command")
        values = [float(t) for t in tokens[index : index + count]]
        if not all(math.isfinite(v) and abs(v) <= 100000 for v in values):
            raise ValueError("Nonfinite or excessive SVG coordinate")
        index += count
        origin = cursor if command.islower() else 0j
        points = [
            complex(values[i], values[i + 1]) + origin for i in range(0, count - 1, 2)
        ]
        if kind == "M":
            if result:
                raise ValueError("Each edge must have one connected subpath")
            cursor = points[0]
            append(cursor)
            command = "l" if command.islower() else "L"
        elif not result:
            raise ValueError("SVG path must start with MoveTo")
        elif kind in {"L", "H", "V"}:
            cursor = (
                complex(values[0] + origin.real, cursor.imag)
                if kind == "H"
                else complex(cursor.real, values[0] + origin.imag)
                if kind == "V"
                else points[0]
            )
            append(cursor)
        else:
            if kind in {"S", "T"}:
                reflected = (
                    2 * cursor - control
                    if previous in ({"C", "S"} if kind == "S" else {"Q", "T"})
                    else cursor
                )
                points.insert(0, reflected)
            curve([cursor, *points])
            control = points[-2]
            cursor = points[-1]
        previous = kind
    if len(result) < 2:
        raise ValueError("Empty SVG edge")
    return result


def join_station_centers(points, start, end):
    """Taper corrections only inside the station neighborhood, preserving mid-edge geometry."""
    lengths = [0.0]
    for a, b in pairwise(points):
        lengths.append(lengths[-1] + abs(b - a))
    length = lengths[-1]
    radius = min(40.0, length / 3)
    if radius <= 0:
        raise ValueError("Zero-length edge")
    result = []
    for i, (a, b) in enumerate(pairwise(points)):
        steps = max(1, math.ceil(abs(b - a) / 4))
        for j in range(steps):
            t = j / steps
            p = a + (b - a) * t
            d = lengths[i] + abs(b - a) * t

            def blend(distance):
                u = max(0.0, 1 - distance / radius)
                return u * u * (3 - 2 * u)

            result.append(
                p
                + (start - points[0]) * blend(d)
                + (end - points[-1]) * blend(length - d)
            )
    result.append(end)
    return [[p.real, p.imag] for p in result]
