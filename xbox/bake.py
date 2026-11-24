"""Bake ac_desert and the bundled MD2 meshes into Xbox-ready data."""
import gzip
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / ".deps"))
import numpy as np
import soundfile

PKG = ROOT.parent / "packages"
OUT = ROOT / "generated"
OUT.mkdir(exist_ok=True)


def bake():
    data = gzip.decompress((PKG / "maps/official/ac_desert.cgz").read_bytes())
    magic, version, offset, factor, count = struct.unpack_from("<4s4i", data)
    if (magic, version, factor) != (b"ACMP", 10, 8):
        raise ValueError("Expected the bundled v10 ac_desert map")
    config = data[980:offset]
    names = re.findall(rb'^texture 0 "([^"]+)"', config, re.M)
    starts = []
    pickups = []
    item_kinds = {3: "I_CLIPS", 4: "I_AMMO", 6: "I_HEALTH", 7: "I_HELMET", 8: "I_ARMOUR"}
    for i in range(count):
        x, y, z, yaw, kind, team = struct.unpack_from("<4h2B", data, offset + 16*i)
        if kind == 2 and team == 100:
            starts.append((x, y, yaw))
        elif kind in item_kinds:
            pickups.append((x, y, item_kinds[kind]))
    pos = offset + 16*count
    cells = bytearray()
    while len(cells) < (1 << (2*factor))*8:
        kind = data[pos]
        pos += 1
        if kind == 255:
            cells.extend(cells[-8:] * data[pos])
            pos += 1
        elif kind == 0:
            wall, delta = data[pos:pos+2]
            cells.extend((0, 0, 0, wall, 0, 0, wall, 0))
            pos += 2
        elif kind in (1, 2, 3, 4, 253):
            floor, ceil, wall, ft, ct, delta, upper, tag = data[pos:pos+8]
            cells.extend((0 if kind == 253 else kind, floor, ceil, wall, ft, ct, upper, tag))
            pos += 8
        else:
            raise ValueError(f"Unexpected cube type {kind}")
    if len(cells) != 256*256*8 or not starts:
        raise ValueError("Invalid map geometry or missing player starts")
    for index in (3, 4, 5, 6):
        if max(cells[index::8]) >= len(names):
            raise ValueError("Map references a missing texture")
    (OUT / "world.bin").write_bytes(cells)
    (OUT / "starts.h").write_text("static const short starts[][3] = {\n" +
        "".join(f"    {{{x}, {y}, {yaw}}},\n" for x,y,yaw in starts) + "};\n")
    (OUT / "items.h").write_text("static const ItemLocation locations[] = {\n" +
        "".join(f"    {{{x}, {y}, {kind}}},\n" for x, y, kind in pickups) + "};\n")
    from bake_gpu import bake_gpu
    entities = [struct.unpack_from("<4h4B", data, offset+16*i) for i in range(count)]
    bake_gpu(OUT, PKG, names, cells, entities)
    for name in ("auto", "auto_reload", "usp", "pistol_reload"):
        samples, rate = soundfile.read(PKG/"audio/weapon"/(name+".ogg"), always_2d=True)
        mono = samples.mean(axis=1)
        sample = np.interp(np.arange(0, len(mono), rate/48000), np.arange(len(mono)), mono)
        (OUT/(name+".bin")).write_bytes((np.clip(sample,-1,1)*24000).astype("<i2").tobytes())


if __name__ == "__main__":
    bake()
