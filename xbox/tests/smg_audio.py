"""Measure intervals between local SMG shot onsets in a 48 kHz HDMI capture."""
import argparse
from pathlib import Path
import wave

import numpy as np


def intervals(path, attack):
    with wave.open(str(path), "rb") as wav:
        if (wav.getframerate(), wav.getnchannels(), wav.getsampwidth()) != (48000, 2, 2):
            raise ValueError(f"{path}: expected stereo 48 kHz 16-bit PCM")
        pcm = np.frombuffer(wav.readframes(wav.getnframes()), "<i2")
    audio = pcm.reshape(-1, 2).mean(axis=1)
    if len(audio) < len(attack):
        raise ValueError(f"{path}: capture shorter than one shot")
    correlation = np.correlate(audio, attack, "valid") / (attack @ attack)
    peaks = []
    for i in np.flatnonzero(correlation > .38):
        if not peaks or i-peaks[-1] > .045*48000:
            peaks.append(i)
        elif correlation[i] > correlation[peaks[-1]]:
            peaks[-1] = i
    gaps = np.diff(peaks) / 48
    # Exclude breaks between bursts. The SMG's nominal interval is 80 ms.
    return len(peaks), gaps[gaps < 120]


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", nargs="+", type=Path)
    args = parser.parse_args()
    clip = Path(__file__).resolve().parents[1]/"generated"/"sub.bin"
    attack = np.fromfile(clip, "<i2", count=1920).astype(float)
    for path in args.capture:
        shots, gaps = intervals(path, attack)
        if not len(gaps):
            parser.error(f"{path}: no local SMG burst detected")
        print(f"{path.name}: {shots} shots, {len(gaps)} within-burst intervals, "
              f"{gaps.min():.2f} to {gaps.max():.2f} ms, "
              f"mean absolute error {np.abs(gaps-80).mean():.2f} ms")
