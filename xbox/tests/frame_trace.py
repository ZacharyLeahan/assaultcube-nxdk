"""Summarize an Xbox frame-trace.bin captured during an ac_desert duel."""
import argparse
from collections import Counter
from pathlib import Path
from struct import Struct

ROW = Struct("<4I2fI")


def summarize(path):
    data = path.read_bytes()
    frequency = int.from_bytes(data[:4], "little")
    if not frequency or len(data) != 4 + 720 * ROW.size:
        raise ValueError(f"{path}: incomplete frame trace")
    frames = list(ROW.iter_unpack(data[4:]))
    pairs = list(zip(frames, frames[1:]))
    intervals = sorted(((b[0] - a[0]) & 0xffffffff) * 1000 / frequency for a, b in pairs)
    refreshes = Counter((b[1] - a[1]) & 0xffffffff for a, b in pairs)
    mismatches = sum(b[2] != a[3] for a, b in pairs)
    print(f"{path.name}: {len(frames)} frames, refresh intervals {dict(sorted(refreshes.items()))}")
    print(f"Median {intervals[len(intervals)//2]:.2f} ms, worst {intervals[-1]:.2f} ms")
    print(f"Previous scanout address mismatches: {mismatches}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path, nargs="+")
    for trace in parser.parse_args().trace:
        summarize(trace)
