"""Summarize an Xbox frame-trace.bin captured during an ac_desert duel."""
import argparse
from collections import Counter
from pathlib import Path
from struct import Struct

ROW = Struct("<4I2fI")
INPUT_TIME = Struct("<3I")


def summarize(path, work_path=None):
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

    if work_path is None:
        return
    data = work_path.read_bytes()
    if len(data) != len(frames) * INPUT_TIME.size:
        raise ValueError(f"{work_path}: incomplete input timings")
    for i, (sample, work, _) in enumerate(INPUT_TIME.iter_unpack(data)):
        until_present = (frames[i][0] - sample) & 0xffffffff
        previous_interval = (frames[i][0] - frames[i-1][0]) & 0xffffffff if i else None
        if work > until_present or (previous_interval is not None and until_present > previous_interval):
            raise ValueError(f"{work_path}: timing row {i} falls outside its frame")
        if i and frames[i][6] != frames[i-1][6]:
            event = "Death camera" if frames[i][6] else "First-person return"
            refreshes = (frames[i][1] - frames[i-1][1]) & 0xffffffff
            print(f"{event} at frame {i}: {work*1000/frequency:.2f} ms from input sample "
                  f"through GPU completion, elapsed refreshes {refreshes}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path, nargs="+")
    parser.add_argument("--work", type=Path, help="input-times.bin from the same capture")
    args = parser.parse_args()
    if args.work and len(args.trace) != 1:
        parser.error("--work requires exactly one frame trace")
    for trace in args.trace:
        summarize(trace, args.work)
