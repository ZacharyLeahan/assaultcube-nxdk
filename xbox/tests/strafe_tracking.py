"""Measure crosshair-to-torso error during the opening life of an Xbox duel."""
import argparse
from math import cos, hypot, sin
from pathlib import Path
from statistics import median
from struct import Struct

ROW = Struct("<10f3I")


def opening(path):
    samples = []
    data = path.read_bytes()
    focal_pixels = 540  # Original headerless 720p traces.
    if data.startswith(b"ACT1"):
        header = Struct("<4sf")
        _, focal_pixels = header.unpack_from(data)
        data = data[header.size:]
    if len(data) != 720 * ROW.size:
        raise ValueError(f"{path}: incomplete tracking trace")
    for frame, row in enumerate(ROW.iter_unpack(data)):
        x, y, eye, yaw, pitch, tx, ty, chest, strafe, turn, live, clear, hit = row
        if not live:
            break
        dx, dy, dz = tx - x, ty - y, chest - eye
        along = dx * cos(yaw) + dy * sin(yaw)
        depth = along * cos(pitch) + dz * sin(pitch)
        if not clear or not strafe or depth <= 0:
            continue
        horizontal = focal_pixels * (dy * cos(yaw) - dx * sin(yaw)) / depth
        vertical = focal_pixels * (along * sin(pitch) - dz * cos(pitch)) / depth
        samples.append((frame, horizontal, vertical, strafe, turn, hit))
    return samples


def summarize(rows):
    if not rows:
        raise ValueError("No visible strafing frames in the opening engagement")
    errors = sorted(hypot(row[1], row[2]) for row in rows)
    return (len(rows), median(errors), errors[int(.9 * (len(errors) - 1))],
            sum(row[5] for row in rows) / len(rows))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path, nargs="+")
    for path in parser.parse_args().trace:
        count, med, p90, hit = summarize(opening(path))
        print(f"{path.name}: {count} visible strafe frames, median {med:.1f} px, "
              f"p90 {p90:.1f} px, crosshair intersected bot on {hit:.1%}")
