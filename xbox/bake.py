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
from PIL import Image

PKG = ROOT.parent / "packages"
OUT = ROOT / "generated"
OUT.mkdir(exist_ok=True)


def pixels(name, rgb):
    Image.fromarray(rgb).save(OUT / (name + ".png"))
    # Xbox framebuffer uses little-endian BGRA.
    (OUT / (name + ".bin")).write_bytes(rgb[:, :, [2, 1, 0, 3]].tobytes())


def sprite(name, model, skin, angle, width, height, frame_index=0):
    data = model.read_bytes()
    _, _, sw, sh, stride, _, nv, nt, nf, _, _, _, uvs, triangles, frame, _, _ = struct.unpack_from("<17i", data)
    frame += frame_index * stride
    scale = np.array(struct.unpack_from("<3f", data, frame))
    vertices = np.frombuffer(data, np.uint8, nv * 4, frame + 40).reshape(-1, 4)[:, :3] * scale
    tex = np.asarray(Image.open(skin).convert("RGB"))
    uv = np.frombuffer(data, "<i2", nt * 2, uvs).reshape(-1, 2) / [sw, sh]
    faces = np.frombuffer(data, "<u2", nf * 6, triangles).reshape(-1, 6)
    x, y, z = vertices.T
    s, c = np.sin(angle), np.cos(angle)
    depth = x * c + y * s
    flat = np.stack((x * s - y * c, -z + depth * 0.13), axis=1)
    flat -= flat.min(axis=0)
    flat *= min((width - 2) / flat[:, 0].max(), (height - 2) / flat[:, 1].max())
    flat += ([width, height] - flat.max(axis=0)) / 2
    result = np.zeros((height, width, 4), dtype=np.uint8)
    zbuf = np.full((height, width), -np.inf)
    for face in faces:
        p, q, r = flat[face[:3]]
        a, b = q - p, r - p
        det = a[0] * b[1] - a[1] * b[0]
        if abs(det) < 0.001:
            continue
        lo = np.floor(np.minimum(np.minimum(p, q), r)).astype(int)
        hi = np.ceil(np.maximum(np.maximum(p, q), r)).astype(int)
        yy, xx = np.mgrid[lo[1]:hi[1], lo[0]:hi[0]]
        dx, dy = xx + 0.5 - p[0], yy + 0.5 - p[1]
        u, v = (dx*b[1]-dy*b[0])/det, (a[0]*dy-a[1]*dx)/det
        weights = np.stack((1-u-v, u, v), axis=-1)
        d = weights @ depth[face[:3]]
        mask = (weights.min(axis=-1) >= 0) & (d > zbuf[yy, xx])
        coords = (weights[mask] @ uv[face[3:]] * [tex.shape[1], tex.shape[0]]).astype(int)
        iy, ix = yy[mask], xx[mask]
        result[iy, ix, :3] = tex[coords[:, 1] % tex.shape[0], coords[:, 0] % tex.shape[1]]
        result[iy, ix, 3] = 255
        zbuf[iy, ix] = d[mask]
    pixels(name, result)


def bake():
    data = gzip.decompress((PKG / "maps/official/ac_desert.cgz").read_bytes())
    magic, version, offset, factor, count = struct.unpack_from("<4s4i", data)
    if (magic, version, factor) != (b"ACMP", 10, 8):
        raise ValueError("Expected the bundled v10 ac_desert map")
    config = data[980:offset]
    names = re.findall(rb'^texture 0 "([^"]+)"', config, re.M)
    atlas = []
    for name in names:
        tex = Image.open(PKG / "textures" / name.decode()).convert("RGBA").resize((64, 64), Image.Resampling.LANCZOS)
        atlas.append(np.asarray(tex))
    pixels("atlas", np.concatenate(atlas))
    sky = Image.open(PKG / "textures/skymaps/makke/sand_ft.jpg").convert("RGBA").resize((256, 128))
    pixels("sky", np.asarray(sky))
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
    players = PKG / "models/playermodels"
    sprite("target", players/"tris.md2", players/"CLA/01_redvest.jpg", 0.2, 48, 80)
    poses = []
    for frame in (40, 42, 44, 47):
        sprite("pose", players/"tris.md2", players/"CLA/01_redvest.jpg", .2, 48, 80, frame)
        poses.append((OUT/"pose.bin").read_bytes())
    (OUT/"poses.bin").write_bytes(b"".join(poses))
    icons = Image.open(PKG/"misc/items.png").convert("RGBA")
    tiles = [icons.crop((64*x,64*y,64*x+64,64*y+64)).resize((32,32)) for x,y in
             ((1,0),(2,1),(2,3),(0,2),(3,2))]
    pixels("items", np.concatenate([np.asarray(tile) for tile in tiles]))
    pistol = PKG / "models/weapons/pistol/world"
    sprite("pistol", pistol/"tris.md2", pistol/"skin.jpg", 2.85, 100, 72)
    rifle = PKG / "models/weapons/assault/world"
    sprite("rifle", rifle/"tris.md2", rifle/"skin.jpg", 2.85, 100, 72)
    for name in ("auto", "auto_reload", "usp", "pistol_reload"):
        samples, rate = soundfile.read(PKG/"audio/weapon"/(name+".ogg"), always_2d=True)
        mono = samples.mean(axis=1)
        sample = np.interp(np.arange(0, len(mono), rate/48000), np.arange(len(mono)), mono)
        (OUT/(name+".bin")).write_bytes((np.clip(sample,-1,1)*24000).astype("<i2").tobytes())


if __name__ == "__main__":
    bake()
