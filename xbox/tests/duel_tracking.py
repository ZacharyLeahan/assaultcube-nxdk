"""Summarize a complete Xbox duel saved by duel-trace.on (classic.bin or tuned.bin)."""
import argparse
from math import atan2, degrees, remainder, tau
from pathlib import Path
from statistics import median
from struct import Struct

ROW = Struct("<10f4I")


def summarize(path):
    raw = list(ROW.iter_unpack(path.read_bytes()))
    if not raw:
        raise ValueError(f"{path}: empty capture")
    errors = [degrees(remainder(atan2(r[6]-r[1], r[5]-r[0])-r[3], tau)) for r in raw]
    missed = sum(((b[-1]-a[-1]) & 0xffffffff)-1 for a, b in zip(raw, raw[1:]))
    peaks, away, returns = [], [], []
    for i in range(1, len(raw)):
        a, b = raw[i-1:i+1]
        if not a[10] or not b[10] or not b[11]:
            continue
        if not a[11]:
            end = i
            while end < len(raw) and raw[end][10] and raw[end][11] and not raw[end][12]:
                end += 1
            returns.append(end-i if end < len(raw) and all(raw[end][10:13]) else None)
        if not a[11]:
            continue
        # A fine turn away starts on-target with the target on the opposite side.
        if a[12] and .02 <= abs(b[9]) <= .25 and b[9]*errors[i-1] < 0 and a[9]*b[9] <= 0:
            away.append(remainder(b[3]-a[3], tau)*b[9] > 0)
        if a[8]*b[8] >= 0:
            continue
        # Measure the first excursion past center in the 250 ms after a reversal.
        sign = 1 if errors[i-1] >= 0 else -1
        peak = 0
        for row, error in zip(raw[i:i+15], errors[i:i+15]):
            if not row[10] or not row[11] or row[8]*b[8] <= 0:
                break
            beyond = -sign*error
            if peak and beyond <= 0:
                break
            peak = max(peak, beyond)
        peaks.append(peak)
    reacquired = [n for n in returns if n is not None]
    print(f"{path.name}: {len(raw)} frames, {missed} missed refreshes")
    if peaks:
        print(f"  {len(peaks)} direct strafe reversals; median excursion past center {median(peaks):.2f} deg")
    print(f"  Small away inputs turning immediately: {sum(away)}/{len(away)}")
    print(f"  Reacquired before losing sight or life: {len(reacquired)}/{len(returns)}",
          f"(median {median(reacquired):g} frames)" if reacquired else "")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path, nargs="+")
    for path in parser.parse_args().trace:
        summarize(path)

