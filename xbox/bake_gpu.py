"""Build GPU texture uploads and material batches from the original ac_desert assets."""
from collections import defaultdict
from functools import lru_cache
from pathlib import Path
import re
import struct
import zlib

import numpy as np
from PIL import Image


def bake_gpu(out, packages, names, cells, entities):
    blob = bytearray()
    textures, batches, symbols = [], [], []
    vertices = bytearray()
    vertex = struct.Struct("<5f4B")

    def texture(image, size, mip=True):
        raw = image.convert("RGBA").resize(size, Image.Resampling.LANCZOS).tobytes()
        packed = zlib.compress(raw, 6)
        textures.append((len(blob), len(packed), *size, int(mip)))
        blob.extend(packed)
        return len(textures)-1

    def mark(name, value):
        symbols.append(f"#define {name} {value}\n")

    def add_batch(tex, points, primitive):
        start = len(vertices)//vertex.size
        for xyz, uv, rgb in points:
            vertices.extend(vertex.pack(*xyz, *uv, *rgb, 255))
        batches.append((tex, start, len(vertices)//vertex.size-start, primitive))

    for name in names:
        texture(Image.open(packages/"textures"/name.decode()), (256,256))
    world = np.frombuffer(cells,np.uint8).reshape(256,256,8)
    lights = np.array([(x,y,z,max(2,r),red,green or red,blue or red)
                       for x,y,z,r,kind,red,green,blue in entities if kind == 1 and r>0])
    @lru_cache(None)
    def light(x,y,z,face):
        # Map light radii and colors modulate vertex albedo; directional ambient keeps outdoor walls readable.
        base = (0.73,0.59,0.80,0.68)[face]
        delta = lights[:,:3] - (x,y,z)
        weight = np.maximum(0,1-np.sqrt((delta*delta).sum(axis=1))/lights[:,3])
        rgb = np.minimum(255, base*235 + (weight[:,None]*lights[:,4:]).sum(axis=0)*.53)
        return tuple(rgb.astype(int))

    # Each coplanar grid stores its material. Merge neighboring cells with identical surfaces.
    planes = defaultdict(dict)
    for y in range(1,255):
        for x in range(1,255):
            kind,f,c,w,ft,ct,upper,_ = map(int,world[y,x])
            if kind < 2:
                continue
            f,c = (f+128)%256-128, (c+128)%256-128
            planes[0,f,False][x,y] = ft
            if ct: planes[1,c,True][x,y] = ct
            for dx,dy in ((1,0),(-1,0),(0,1),(0,-1)):
                nk,nf,nc,nw,_,_,nu,_ = map(int,world[y+dy,x+dx])
                nf,nc = (nf+128)%256-128,(nc+128)%256-128
                axis,coord,u = (2,x+(dx>0),y) if dx else (3,y+(dy>0),x)
                walls = [(f,c,nw)] if nk<2 else [(f,min(c,nf),nw),(max(f,nc),c,nu)]
                for low,high,material in walls:
                    for z in range(low,high):
                        planes[axis,coord,dx>0 or dy<0][u,z] = material
    groups = defaultdict(list)
    for (face,level,reverse), surface in planes.items():
        for a,b in sorted(surface,key=lambda k:(k[1],k[0])):
            tex = surface.get((a,b))
            if tex is None: continue
            width = height = 1
            while width<16 and surface.get((a+width,b))==tex: width+=1
            while height<16 and all(surface.get((a+i,b+height))==tex for i in range(width)): height+=1
            for j in range(height):
                for i in range(width): del surface[a+i,b+j]
            corners=((a,b),(a+width,b),(a+width,b+height),(a,b+height))
            for u,v in (reversed(corners) if reverse else corners):
                xyz = (u,v,level) if face<2 else (level,u,v) if face==2 else (u,level,v)
                groups[tex].append((xyz,(u/8,(v if face<2 else -v)/8),light(*xyz,face)))
    for tex,points in sorted(groups.items()): add_batch(tex,points,4)
    mark("WORLD_BATCHES",len(batches))

    mark("SKY_TEX",len(textures))
    for side in ("ft","rt","bk","lf","up","dn"):
        texture(Image.open(packages/f"textures/skymaps/makke/sand_{side}.jpg"),(256,256),False)

    def model_skin(path,size):
        # Raise shadows in the original dark skins while preserving highlights.
        skin=Image.open(path).convert("RGB")
        curve=[round(255*(n/255)**.70) for n in range(256)]
        return texture(skin.point(curve*3),size)

    def md2(path, skin, size):
        d=(path/"tris.md2").read_bytes()
        _,_,sw,sh,stride,_,nv,nt,nf,_,_,_,st,tr,frame,_,_=struct.unpack_from("<17i",d)
        uv=np.frombuffer(d,"<i2",nt*2,st).reshape(-1,2)/[sw,sh]
        faces=np.frombuffer(d,"<u2",nf*6,tr).reshape(-1,6)
        tex=model_skin(skin,size)
        for pose in (0,40,41,42,43,44,45,47):
            off=frame+pose*stride
            scale=np.array(struct.unpack_from("<3f",d,off))
            pos=np.frombuffer(d,np.uint8,nv*4,off+40).reshape(-1,4)[:,:3]*scale+struct.unpack_from("<3f",d,off+12)
            pos=(pos+[0,0,24.15])*.086
            model_batch(tex,pos,uv,faces[:,:3],faces[:,3:])

    def model_batch(tex,pos,uv,tri,st):
        pts=[]
        for idx,t in zip(tri,st):
            a,b,c=pos[idx]
            normal=np.cross(b-a,c-a)
            n=normal/max(.0001,np.linalg.norm(normal))
            shade=int(np.clip(207+36*n[2]-14*n[0],150,255))
            for v,k in zip(idx,t): pts.append((pos[v],uv[k],(shade,)*3))
        add_batch(tex,pts,3)

    mark("BODY_BATCH",len(batches))
    player=packages/"models/playermodels"
    md2(player,player/"CLA/01_redvest.jpg",(256,256))
    mark("BOT_GUN_BATCH",len(batches))
    gun=packages/"models/weapons/assault/world"
    md2(gun,gun/"skin.jpg",(128,64))

    hands=model_skin(packages/"models/weapons/knife/skin_hands.jpg",(256,256))
    for name in ("assault","pistol"):
        folder=packages/"models/weapons"/name
        d=(folder/"tris_high.md3").read_bytes()
        tex=model_skin(folder/"skin_weapon.jpg",(256,256))
        header=struct.unpack_from("<4si64s9i",d)
        mark(name.upper()+"_BATCH",len(batches))
        for pose in (0,10):
            off=header[-2]
            for _ in range(header[6]):
                _,label,_,_,_,nv,nt,tr,_,st,xyz,end=struct.unpack_from("<4s64s10i",d,off)
                pos=np.frombuffer(d,"<i2",nv*4,off+xyz+pose*nv*8).reshape(-1,4)[:,:3]/64
                uv=np.frombuffer(d,"<f4",nv*2,off+st).reshape(-1,2)
                tri=np.frombuffer(d,"<i4",nt*3,off+tr).reshape(-1,3)
                # MD3 weapon assets point down +Y, with Z up and the grip on camera right.
                model_batch(hands if label.rstrip(b"\0")==b"hands" else tex,pos,uv,tri,tri)
                off+=end

    mark("ITEM_TEX",len(textures))
    icons = Image.open(packages/"misc/items.png").convert("RGBA")
    atlas = Image.new("RGBA",(32,256))
    for index,(x,y) in enumerate(((1,0),(2,1),(2,3),(0,2),(3,2))):
        tile=icons.crop((64*x,64*y,64*x+64,64*y+64)).resize((32,32))
        atlas.paste(tile,(0,index*32))
    texture(atlas,(32,256),False)
    mark("SOLID_TEX",len(textures))
    texture(Image.new("RGBA",(4,4),"white"),(4,4),False)
    mark("FONT_TEX",len(textures))
    texture(Image.open(packages/"misc/chars_default.png"),(512,512),False)
    glyphs=re.findall(r"^fontchar\s+(-?\d+)\s+(-?\d+)\s+(\d+)\s+(\d+)",
                      (packages.parent/"config/font_default.cfg").read_text(),re.M)[:94]
    mark("VERTEX_BYTES",len(vertices))
    (out/"gpu_textures.bin").write_bytes(blob)
    (out/"gpu_vertices.bin").write_bytes(vertices)
    def rows(name,kind,data):
        return f"static const {kind} {name}[] = {{\n"+ "".join("    {"+",".join(map(str,r))+"},\n" for r in data)+"};\n"
    (out/"gpu.h").write_text("".join(symbols)+rows("images","ImageInfo",textures)+rows("batches","Batch",batches)+rows("glyphs","Glyph",glyphs))
