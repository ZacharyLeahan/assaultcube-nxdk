"""Measure overlapping rifle reports in a 48 kHz Xbox HDMI recording."""
import argparse
from pathlib import Path
import wave

import numpy as np


def measure(path, clip_path, seconds):
    with wave.open(str(path), "rb") as wav:
        rate = wav.getframerate()
        if (rate, wav.getnchannels(), wav.getsampwidth()) != (48000, 2, 2):
            raise ValueError("Expected stereo 48 kHz signed 16-bit PCM")
        pcm = np.frombuffer(wav.readframes(round(seconds*rate)), "<i2").reshape(-1, 2)
    audio = pcm.mean(axis=1) / 32768
    shot = np.fromfile(clip_path, "<i2").astype(float) / 32768
    attack = shot[:1536]
    if len(audio) < len(attack):
        raise ValueError("Recording is shorter than one rifle attack")
    correlation = np.correlate(audio, attack, "valid") / np.dot(attack, attack)
    # Separate the loud local attacks from quieter positional bot gunfire.
    threshold = max(.10, correlation.max() * .60)
    peaks = []
    for start in range(0, len(correlation), 2048):
        i = start + np.argmax(correlation[start:start+2048])
        if correlation[i] < threshold:
            continue
        if peaks and i-peaks[-1] < .085*rate:
            if correlation[i] > correlation[peaks[-1]]:
                peaks[-1] = i
        else:
            peaks.append(i)

    gains = []
    for k, onset in enumerate(peaks):
        if not k or onset-peaks[k-1] > .16*rate:
            continue
        n = min(2200, len(audio)-onset)
        tails = np.zeros(n)
        for previous in peaks[:k]:
            offset = onset-previous
            count = min(n, len(shot)-offset)
            if count > 0:
                tails[:count] += shot[offset:offset+count]
        gains.append(np.linalg.lstsq(np.column_stack((shot[:n], tails)),
                                     audio[onset:onset+n], rcond=None)[0])
    if not gains:
        raise ValueError("No repeated local rifle shots detected")
    direct, tail = np.median(gains, axis=0)
    print(f"{path.name}: {len(gains)} repeated shots, median direct gain {direct:.3f}, "
          f"prior-shot gain {tail:.3f}; {np.count_nonzero((pcm==-32768)|(pcm==32767))} saturated samples")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("recordings", type=Path, nargs="+")
    parser.add_argument("--seconds", type=float, default=4)
    parser.add_argument("--clip", type=Path, default=Path(__file__).resolve().parents[1]/"generated/auto.bin")
    args = parser.parse_args()
    for recording in args.recordings:
        measure(recording, args.clip, args.seconds)
