"""Measure XID report arrival to presentation of the first frame with changed yaw."""
import argparse
import math
import statistics
import struct
from bisect import bisect_left
from collections import defaultdict
from pathlib import Path


def measure(usb, trace):
    data = trace.read_bytes()
    hz, = struct.unpack_from("<I", data)
    frames = list(struct.iter_unpack("<4I2fI", data[4:]))
    origin = frames[0][0]
    def seconds(tick):
        return ((tick - origin + 2**31) % 2**32 - 2**31) / hz
    times = [seconds(frame[0]) for frame in frames]
    groups = defaultdict(list)
    excluded = 0
    for tick, port, axis in struct.iter_unpack("<IIi", usb.read_bytes()):
        if abs(axis) not in (5000, 19000):
            continue
        start = seconds(tick)
        i = bisect_left(times, start)
        if i < 2 or i >= len(frames) or start+.10 > times[-1]:
            excluded += 1
            continue
        base = frames[i-1][4]
        end = bisect_left(times, start+.10)
        if any(row[6] for row in frames[i-2:end]) or frames[i-2][4] != base:
            excluded += 1
            continue
        delay = None
        for j in range(i, end):
            angle = math.remainder(frames[j][4]-base, 2*math.pi)
            if angle * axis > 0 and abs(angle) > 0.00001:
                delay = 1000*(times[j]-start)
                break
        groups[abs(axis)].append(delay)
    print(f"{excluded} events excluded (capture boundary, respawn, or camera already moving)")
    for magnitude, trial in sorted(groups.items()):
        hits = sorted(v for v in trial if v is not None)
        summary = ""
        if hits:
            summary = f", min/median/max {min(hits):.1f}/{statistics.median(hits):.1f}/{max(hits):.1f} ms"
        print(f"{magnitude}/32768: {len(hits)}/{len(trial)} turns detected within 100 ms{summary}")


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("usb", type=Path)
    p.add_argument("frames", type=Path)
    a = p.parse_args()
    measure(a.usb, a.frames)
