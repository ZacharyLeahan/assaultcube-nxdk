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


def bake_ambience(config, entities):
    slots = re.findall(rb'^mapsound "([^"]+)" (-?\d+)', config, re.M)
    bank = bytearray()
    clips = []
    for slot, maxuses in slots:
        if int(maxuses)!=-1:
            raise ValueError("Expected unrestricted ac_desert sound slots")
        samples, rate = soundfile.read(PKG/"audio"/slot.decode(), always_2d=True)
        if samples.shape[1] not in (1,2) or not len(samples):
            raise ValueError(f"Expected nonempty mono or stereo audio in {slot!r}")
        positions = np.arange(0, len(samples), rate/48000)
        pcm = np.column_stack([np.interp(positions, np.arange(len(samples)), channel)
                               for channel in samples.T])
        # Mu-law keeps the stereo wind and long quiet tails in half the PCM space.
        encoded = np.rint(np.sign(pcm)*127*np.log1p(255*np.minimum(1,abs(pcm)))/np.log(256))
        clips.append((len(bank), len(pcm), pcm.shape[1]))
        bank.extend((128+encoded).astype(np.uint8).tobytes())
    emitters = [(x,y,z,slot,outer,inner,volume) for x,y,z,slot,kind,outer,inner,volume
                in entities if kind==14]
    if any(not 0<=e[3]<len(clips) for e in emitters):
        raise ValueError("Map sound references a missing slot")
    def rows(name, kind, values):
        return f"static const {kind} {name}[] = {{\n"+''.join(
            '    {'+','.join(map(str,row))+'},\n' for row in values)+'};\n'
    codes=np.clip((np.arange(256)-128)/127,-1,1)
    decoded=np.rint(np.sign(codes)*np.expm1(abs(codes)*np.log(256))/255*24000).astype(int)
    (OUT/"ambience.bin").write_bytes(bank)
    (OUT/"ambience.h").write_text(rows("ambient_clips","AmbientClip",clips)+
        rows("emitters","SoundEmitter",emitters)+"static const short decoded[256] = {"+
        ','.join(map(str,decoded))+"};\n")


def bake():
    data = gzip.decompress((PKG / "maps/official/ac_desert.cgz").read_bytes())
    magic, version, offset, factor, count = struct.unpack_from("<4s4i", data)
    if (magic, version, factor) != (b"ACMP", 10, 8):
        raise ValueError("Expected the bundled v10 ac_desert map")
    config = data[980:offset]
    names = re.findall(rb'^texture 0 "([^"]+)"', config, re.M)
    starts = []
    pickups = []
    ladders = []
    clips = []
    item_kinds = {3: "I_CLIPS", 4: "I_AMMO", 6: "I_HEALTH", 7: "I_HELMET", 8: "I_ARMOUR"}
    for i in range(count):
        x, y, z, yaw, kind, team = struct.unpack_from("<4h2B", data, offset + 16*i)
        if kind == 2 and team == 100:
            starts.append((x, y, yaw))
        elif kind in item_kinds:
            pickups.append((x, y, yaw, item_kinds[kind]))
        elif kind == 12:
            ladders.append((x,y,yaw))  # attr1 is height above the cell floor
        elif kind == 15:
            half_x,half_y,height,_,tilt,shape=struct.unpack_from("<BBBh bB",data,offset+16*i+9)
            if tilt or shape not in (0,3):
                raise ValueError("Unsupported ac_desert clip shape")
            clips.append((x,y,yaw,half_x,half_y,height,shape))
    pos = offset + 16*count
    cells = bytearray()
    deltas = bytearray()
    while len(cells) < (1 << (2*factor))*8:
        kind = data[pos]
        pos += 1
        if kind == 255:
            cells.extend(cells[-8:] * data[pos])
            deltas.extend(deltas[-1:] * data[pos])
            pos += 1
        elif kind == 0:
            wall, delta = data[pos:pos+2]
            deltas.append(delta)
            cells.extend((0, 0, 0, wall, 0, 0, wall, 0))
            pos += 2
        elif kind in (1, 2, 3, 4, 253):
            floor, ceil, wall, ft, ct, delta, upper, tag = data[pos:pos+8]
            deltas.append(delta)
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
    water,r,g,b,a=struct.unpack_from("<i4B",data,916)
    surface=water/10-.3
    wet=[(i%256,i//256) for i,(k,f,*_) in enumerate(struct.iter_unpack("<Bbb5B",cells))
         if k>=2 and f<surface]
    patches=sorted({(x//4*4,y//4*4) for x,y in wet})
    (OUT/"water.h").write_text(f"#define WATER_LEVEL ({water/10:.3f}f)\n#define WATER_SURFACE (WATER_LEVEL-.3f)\n"+
        f"static const unsigned char water_color[4] = {{{r},{g},{b},{a}}};\n"+
        "static const short water_patches[][2] = {"+
        ",".join(f"{{{x},{y}}}" for x,y in patches)+"};\n")
    (OUT / "ladders.h").write_text("static const short ladders[][3] = {\n"+
        "".join(f"    {{{x}, {y}, {height}}},\n" for x,y,height in ladders)+"};\n")
    (OUT / "clips.h").write_text("static const MapClip clips[] = {\n"+
        "".join("    {"+", ".join(map(str,c))+"},\n" for c in clips)+"};\n")
    (OUT / "deltas.bin").write_bytes(deltas)
    (OUT / "starts.h").write_text("static const short starts[][3] = {\n" +
        "".join(f"    {{{x}, {y}, {yaw}}},\n" for x,y,yaw in starts) + "};\n")
    (OUT / "items.h").write_text("static const ItemLocation locations[] = {\n" +
        "".join(f"    {{{x}, {y}, {height}, {kind}}},\n" for x, y, height, kind in pickups) + "};\n")
    from bake_corners import map_corners
    corners = map_corners(cells,deltas)
    records = []
    for (x,y),(size,q,inside,outside) in corners.items():
        nx,ny = ((1,1),(1,-1),(-1,-1),(-1,1))[q]
        plane = nx*x+ny*y+(nx+ny)*size//2
        for dy in range(size):
            for dx in range(size):
                records.append(((y+dy)*256+x+dx,plane,nx,ny,int(inside[0]==0),
                                inside[1],inside[2],outside[1],outside[2],inside[3],inside[5],outside[5]))
    (OUT/"corners.h").write_text("static const MapCorner corners[] = {\n"+
        "".join("    {"+",".join(map(str,r))+"},\n" for r in sorted(records))+"};\n")
    from bake_gpu import bake_gpu
    entities = [struct.unpack_from("<4h4B", data, offset+16*i) for i in range(count)]
    bake_ambience(config, entities)
    from bake_lighting import model_lighting
    lighting = model_lighting(cells, entities, *struct.unpack_from("<ii", data, 924))
    (OUT / "cell_light.bin").write_bytes(lighting.tobytes())
    model_slots=re.findall(rb'^mapmodel \d+ \d+ (-?\d+) [\d.]+ "([^"]+)"',config,re.M)
    bake_gpu(OUT, PKG, names, cells, deltas, entities, model_slots, lighting, corners)
    sounds=("weapon/knife","weapon/auto","weapon/auto_reload","weapon/usp","weapon/pistol_reload","weapon/clip_empty",
            "weapon/ric1","weapon/ric2","weapon/ric3","weapon/waterimpact",
            "player/jump","player/land","player/step","player/footsteps","player/crouch","player/watersteps",
            "player/watercrouch","player/crouch_in","player/crouch_out",*(f"player/pain{i}" for i in range(1,7)),
            "player/die1","player/die2","player/splash1","player/splash2",
            "misc/pickup_ammo_clip","misc/pickup_health","misc/pickup_helmet","misc/pickup_armour","misc/change_gun")
    for sound in sounds:
        name=sound.split("/")[-1]
        samples, rate = soundfile.read(PKG/"audio"/(sound+".ogg"), always_2d=True)
        mono = samples.mean(axis=1)
        sample = np.interp(np.arange(0, len(mono), rate/48000), np.arange(len(mono)), mono)
        (OUT/(name+".bin")).write_bytes((np.clip(sample,-1,1)*24000).astype("<i2").tobytes())


if __name__ == "__main__":
    bake()
