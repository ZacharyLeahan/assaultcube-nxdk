"""Build GPU texture uploads and material batches from the original ac_desert assets."""
from collections import defaultdict
from pathlib import Path
import re
import struct
import zlib

import numpy as np
from PIL import Image


def bake_gpu(out, packages, names, cells, deltas, entities, model_slots, lighting, corners):
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

    world_uv=[]
    for name in names:
        with Image.open(packages/"textures"/name.decode()) as image:
            width,height=image.size
            # PC geometry repeats the original image at 32 texels per map unit.
            world_uv.append((32/width,32/height))
            size=tuple(max(4,1<<(n-1).bit_length()) for n in (width,height))
            texture(image,size)

    def world_texcoord(tex,u,v):
        sx,sy=world_uv[tex]
        return (u*sx,v*sy)
    world = np.frombuffer(cells,np.uint8).reshape(256,256,8)
    corner_cells = {}
    for (bx,by),(size,q,inside,outside) in corners.items():
        # Positive values face the narrower (or solid) half in cornertest().
        nx,ny = ((1,1),(1,-1),(-1,-1),(-1,1))[q]
        center = nx*(bx+size*.5)+ny*(by+size*.5)
        for y in range(by,by+size):
            for x in range(bx,bx+size):
                corner_cells[x,y] = nx,ny,center,inside,outside,bx,size

    def corner_profile(x,y,u,v):
        nx,ny,center,inside,outside,_,_ = corner_cells[x,y]
        cell = inside if nx*u+ny*v>center else outside
        # Corner floors and ceilings are flat even next to a heightfield.
        return (4 if cell[0] else 0,*cell[1:])
    # Each coplanar grid stores its material. Merge neighboring cells with identical surfaces.
    planes = defaultdict(dict)
    groups = defaultdict(list)
    def level(kind, base, face, x, y):
        if kind == 2 + face:
            delta = deltas[y*256+x] * .25
            return base + (delta if face else -delta)
        return base

    for y in range(1,255):
        for x in range(1,255):
            kind,f,c,w,ft,ct,upper,_ = map(int,world[y,x])
            if kind == 0:
                continue
            if kind == 1:
                nx,ny,center,inside,outside,bx,size = corner_cells[x,y]
                square = ((x,y),(x+1,y),(x+1,y+1),(x,y+1))
                distances = [nx*u+ny*v-center for u,v in square]
                for sign,cell in ((1,inside),(-1,outside)):
                    if not cell[0]: continue
                    poly = [p for p,d in zip(square,distances) if sign*d>=0]
                    if len(poly)<3: continue
                    for face in (0,1):
                        tex = cell[4+face]
                        points = list(reversed(poly)) if face else list(poly)
                        if len(points)==3: points.append(points[-1])
                        for u,v in points:
                            groups[tex].append(((u,v,cell[1+face]),world_texcoord(tex,u,v),(255,255,255)))
                edge = [p for p,d in zip(square,distances) if d==0]
                if len(edge)==2:
                    a,b = edge
                    # The visible wall normal points toward the open half.
                    if (b[1]-a[1])*nx-(b[0]-a[0])*ny>0: a,b=b,a
                    spans = [(outside[1],outside[2],inside[3])] if not inside[0] else [
                        (outside[1],inside[1],inside[3]),(inside[2],outside[2],inside[6])]
                    for low,high,tex in spans:
                        if high<=low: continue
                        for (u,v),z in ((a,low),(b,low),(b,high),(a,high)):
                            wall_u = u if ny>0 else 2*bx+size-u
                            groups[tex].append(((u,v,z),world_texcoord(tex,wall_u,-z),(255,255,255)))
            f,c = (f+128)%256-128, (c+128)%256-128
            for face,base,tex in (() if kind==1 else ((0,f,ft),(1,c,ct))):
                corners=((x,y),(x+1,y),(x+1,y+1),(x,y+1))
                points=[(u,v,level(kind,base,face,u,v)) for u,v in corners]
                if all(p[2]==points[0][2] for p in points):
                    planes[face,points[0][2],bool(face)][x,y]=tex
                else:
                    for p in ([points[i] for i in (1,0,3,2)] if face else points):
                        groups[tex].append((p,world_texcoord(tex,p[0],p[1]),(255,255,255)))
            for dx,dy in ((1,0),(-1,0),(0,1),(0,-1)):
                u,v=x+.5+.5*dx,y+.5+.5*dy
                edge_kind=kind
                if kind==1:
                    edge_kind,f,c,*_=corner_profile(x,y,u,v)
                    if not edge_kind: continue
                nk,nf,nc,nw,_,_,nu,_ = map(int,world[y+dy,x+dx])
                nf,nc = (nf+128)%256-128,(nc+128)%256-128
                if nk==1:
                    nk,nf,nc,nw,_,_,nu,_=corner_profile(x+dx,y+dy,u,v)
                axis,coord,u = (2,x+(dx>0),y) if dx else (3,y+(dy>0),x)
                if edge_kind in (2,3) or nk in (2,3):
                    endpoints=((coord,u),(coord,u+1)) if dx else ((u,coord),(u+1,coord))
                    floor=[level(edge_kind,f,0,*p) for p in endpoints]
                    ceil=[level(edge_kind,c,1,*p) for p in endpoints]
                    spans=[(floor,ceil,nw)] if nk<2 else [
                        (floor,[min(ceil[i],level(nk,nf,0,*p)) for i,p in enumerate(endpoints)],nw),
                        ([max(floor[i],level(nk,nc,1,*p)) for i,p in enumerate(endpoints)],ceil,nu)]
                    for low,high,tex in spans:
                        if all(h<=l for l,h in zip(low,high)): continue
                        # Trim at the crossing when two ramps meet partway along an edge.
                        ends=[(u+i,low[i],high[i]) for i in range(2)]
                        for i in range(2):
                            if high[i]<low[i]:
                                j=1-i
                                t=(low[i]-high[i])/(high[j]-low[j]+low[i]-high[i])
                                z=low[i]+t*(low[j]-low[i])
                                ends[i]=(u+i+t*(j-i),z,z)
                        a,b=ends
                        corners=((a[0],a[1]),(b[0],b[1]),(b[0],b[2]),(a[0],a[2]))
                        for s,z in (reversed(corners) if dx>0 or dy<0 else corners):
                            p=(coord,s,z) if dx else (s,coord,z)
                            groups[tex].append((p,world_texcoord(tex,s,-z),(255,255,255)))
                    continue
                walls = [(f,c,nw)] if nk<2 else [(f,min(c,nf),nw),(max(f,nc),c,nu)]
                for low,high,material in walls:
                    for z in range(low,high):
                        planes[axis,coord,dx>0 or dy<0][u,z] = material
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
                groups[tex].append((xyz,world_texcoord(tex,u,v if face<2 else -v),(255,255,255)))
    mark("SKY_PORTAL_BATCH",len(batches))
    add_batch(0,groups.pop(0),4)
    for tex,points in sorted(groups.items()): add_batch(tex,points,4)
    mark("WORLD_BATCHES",len(batches))

    mark("SKY_TEX",len(textures))
    for side in ("ft","rt","bk","lf","up","dn"):
        texture(Image.open(packages/f"textures/skymaps/makke/sand_{side}.jpg"),(256,256),False)

    def model_skin(path,size):
        with Image.open(path) as skin:
            return texture(skin,size)

    bot_frames=(*range(58),*range(178,184))
    bot_positions=bytearray()
    symbols.append("static const unsigned char bot_frame_batch[198] = {"+
                   ",".join(f"[{pose}]={i}" for i,pose in enumerate(bot_frames))+"};\n")

    def md2(path, skin, size, name, muzzle=None):
        d=(path/"tris.md2").read_bytes()
        _,_,sw,sh,stride,_,nv,nt,nf,_,_,_,st,tr,frame,commands,_=struct.unpack_from("<17i",d)
        cfg=(path/"md2.cfg").read_text()
        model_scale=float(re.search(r"^mdlscale (\d+)",cfg,re.M)[1])/1600
        translate=np.array(re.search(r"^mdltrans (.+)",cfg,re.M)[1].split(),float)
        heads=[]
        # md2tag indices address the PC loader's unique (UV, vertex)
        # triples in GL command order, not the MD2 frame vertex array.
        indices={}
        while True:
            count=struct.unpack_from("<i",d,commands)[0]
            commands+=4
            if not count: break
            for _ in range(abs(count)):
                key=struct.unpack_from("<3i",d,commands)
                commands+=12
                indices.setdefault(key,key[2])
        vertices=list(indices.values())
        if muzzle is None:
            head_indices=[vertices[int(i)] for i in re.search(r"^md2tag tag_head (.+)",cfg,re.M)[1].split()]
        uv=np.frombuffer(d,"<i2",nt*2,st).reshape(-1,2)/[sw,sh]
        faces=np.frombuffer(d,"<u2",nf*6,tr).reshape(-1,6)
        tex=model_skin(skin,size)
        mark(name+"_VERTICES",nv)
        mark(name+"_POSE_BASE",len(bot_positions)//12)
        symbols.append("static const unsigned short "+name.lower()+"_indices[] = {"+
                       ",".join(map(str,faces[:,:3].flatten()))+"};\n")
        for pose in bot_frames:
            off=frame+pose*stride
            scale=np.array(struct.unpack_from("<3f",d,off))
            pos=np.frombuffer(d,np.uint8,nv*4,off+40).reshape(-1,4)[:,:3]*scale+struct.unpack_from("<3f",d,off+12)
            pos=(pos+translate)*[model_scale,-model_scale,model_scale]
            if muzzle is None:
                heads.append((pose,pos[head_indices].mean(axis=0)))
            if muzzle is not None:
                # assault/world/md2.cfg uses "md2tag tag_muzzle X": select
                # the first maximum-X vertex in frame zero, then track it.
                if pose==0: muzzle_vertex=vertices[np.argmax(pos[vertices,0])]
                muzzle.append(pos[muzzle_vertex])
            bot_positions.extend(pos.astype("<f4").tobytes())
            if pose==0: model_batch(tex,pos,uv,faces[:,:3],faces[:,3:])
        if heads:
            tags=dict(heads)
            (out/"bot_head.h").write_text("static const MatchPoint head_poses[] = {\n"+
                "".join("    {"+",".join(f"{a:.7f}f" for a in tags.get(pose,(0,0,0)))+"},\n"
                        for pose in range(max(tags)+1))+"};\n")

    def model_batch(tex,pos,uv,tri,st):
        pts=[]
        for idx,t in zip(tri,st):
            for v,k in zip(idx,t): pts.append((pos[v],uv[k],(255,255,255)))
        add_batch(tex,pts,3)

    def md3_surfaces(data, header, pose):
        off=header[-2]
        for _ in range(header[6]):
            _,label,_,_,_,nv,nt,tr,_,st,xyz,end=struct.unpack_from("<4s64s10i",data,off)
            pos=np.frombuffer(data,"<i2",nv*4,off+xyz+pose*nv*8).reshape(-1,4)[:,:3]/64
            uv=np.frombuffer(data,"<f4",nv*2,off+st).reshape(-1,2)
            tri=np.frombuffer(data,"<i4",nt*3,off+tr).reshape(-1,3)
            yield label.rstrip(b"\0"),pos,uv,tri
            off+=end

    mark("BODY_BATCH",len(batches))
    player=packages/"models/playermodels"
    md2(player,player/"CLA/01_redvest.jpg",(256,256),"BODY")
    mark("BOT_GUN_BATCH",len(batches))
    gun=packages/"models/weapons/assault/world"
    bot_muzzle=[]
    md2(gun,gun/"skin.jpg",(128,64),"BOT_GUN",bot_muzzle)
    (out/"bot_poses.bin").write_bytes(bot_positions)
    symbols.append("static const float bot_muzzle[][3] = {"+
                   ",".join("{"+",".join(f"{v:.6f}f" for v in p)+"}" for p in bot_muzzle)+"};\n")

    hands=model_skin(packages/"models/weapons/knife/skin_hands.jpg",(256,256))
    for name in ("assault","pistol"):
        folder=packages/"models/weapons"/name
        d=(folder/"tris_high.md3").read_bytes()
        tex=model_skin(folder/"skin_weapon.jpg",(256,256))
        header=struct.unpack_from("<4si64s9i",d)
        mark(name.upper()+"_BATCH",len(batches))
        muzzle=[]
        for pose in (0,*range(8,16)):
            for tag in range(header[5]):
                label,*origin=struct.unpack_from("<64s3f",d,header[-3]+112*(pose*header[5]+tag))
                if label.rstrip(b"\0")==b"tag_muzzle": muzzle.append(tuple(origin))
            for label,pos,uv,tri in md3_surfaces(d,header,pose):
                # MD3 weapon assets point down +Y, with Z up and the grip on camera right.
                model_batch(hands if label==b"hands" else tex,pos,uv,tri,tri)
        if len(muzzle)!=9: raise ValueError(f"Missing muzzle tag for {name}")
        symbols.append(f"static const float {name}_muzzle[][3] = {{"+
                       ",".join("{"+",".join(map(str,p))+"}" for p in muzzle)+"};\n")

    mark("PICKUP_BATCH",len(batches))
    for name in ("pistolclips","ammobox","health","helmet","kevlar"):
        folder=packages/"models/pickups"/name
        d=(folder/(name+".md3")).read_bytes()
        header=struct.unpack_from("<4si64s9i",d)
        if header[6]!=1: raise ValueError(f"Expected one pickup mesh for {name}")
        tex=model_skin(folder/"skin.jpg",(128,128))
        for _,pos,uv,tri in md3_surfaces(d,header,0):
            pos=pos[:,[1,0,2]]
            if name=="helmet": pos[:,2]-=15  # mdltrans in the bundled md3.cfg
            model_batch(tex,pos/16,uv,tri,tri)
    mark("PROP_START",len(batches))
    prop_bounds=[]
    prop_lights=[]
    for slot,(offset,name) in enumerate(model_slots):
        folder=packages/"models/mapmodels"/name.decode()
        cfg=(folder/"md3.cfg").read_text()
        mesh=re.search(r"^md3load (\S+)",cfg,re.M)
        skin=re.search(r"^md3skin \S+ (\S+)",cfg,re.M)
        d=(folder/(mesh[1] if mesh else "tris.md3")).read_bytes()
        tex=texture(Image.open(folder/(skin[1] if skin else "skin.jpg")),(128,128))
        header=struct.unpack_from("<4si64s9i",d)
        points=[]
        for _,pos,uv,tri in md3_surfaces(d,header,0):
            local=pos[:,[1,0,2]]/16
            for x,y,_,yaw,kind,index,elevation,_ in entities:
                if kind!=10 or index!=slot: continue
                angle=np.radians(yaw*.1+180)
                c,s=np.cos(angle),np.sin(angle)
                xyz=local@np.array([[c,s,0],[-s,c,0],[0,0,1]])
                z=int(world[y,x,1].view(np.int8))+int(offset)+elevation/5
                xyz+=[x,y,z]
                rgb=lighting[y,x]
                first=len(vertices)//vertex.size+len(points)
                points.extend((xyz[v],uv[v],rgb) for face in tri for v in face)
                prop_lights.append((x,y,first,tri.size))
                bounds=(xyz.min(axis=0).astype(np.float32),xyz.max(axis=0).astype(np.float32))
                limits=["{"+",".join(format(n,".9g") for n in side)+"}" for side in bounds]
                prop_bounds.append("    {"+",".join([*limits,str(first),str(tri.size)])+"},\n")
        if points: add_batch(tex,points,3)
    mark("PROP_END",len(batches))
    symbols.append("static const PropLightSpan prop_lights[] = {\n"+
                   "".join("    {"+",".join(map(str,row))+"},\n" for row in prop_lights)+"};\n")
    (out/"prop_bounds.h").write_text("static const PropMesh props[] = {\n"+"".join(prop_bounds)+"};\n")
    icons = Image.open(packages/"misc/items.png").convert("RGBA")
    # The PC renders a square overhead view of the occupied map bounds.
    ys,xs=np.nonzero(world[:,:,0])
    width,height=int(xs.max()-xs.min()+1),int(ys.max()-ys.min()+1)
    span=max(width,height)
    left=float(xs.min())-(span-width)*.5
    top=float(ys.min())-(span-height)*.5
    mark("MAP_SPAN",span)
    mark("MAP_LEFT",left)
    mark("MAP_TOP",top)
    tiles=np.array([np.asarray(Image.open(packages/"textures"/n.decode()).convert("RGB").resize((16,16)))
                    for n in names])
    yy,xx=np.indices((512,512))
    px=left+(xx+.5)*span/512
    py=top+(yy+.5)*span/512
    x,y=px.astype(int),py.astype(int)
    slot=world[y,x,4]
    solid=world[y,x,0]==0
    for row,col in zip(*np.nonzero(world[y,x,0]==1)):
        profile=corner_profile(x[row,col],y[row,col],px[row,col],py[row,col])
        solid[row,col]=not profile[0]
        slot[row,col]=profile[4]
    scale=np.asarray(world_uv)[slot]
    u=(px*16*scale[:,:,0]).astype(int)%16
    v=(py*16*scale[:,:,1]).astype(int)%16
    fx,fy=(px-x)[:,:,None],(py-y)[:,:,None]
    light=((1-fy)*((1-fx)*lighting[y,x]+fx*lighting[y,x+1])
            + fy*((1-fx)*lighting[y+1,x]+fx*lighting[y+1,x+1]))
    terrain=np.minimum(255,tiles[slot,v,u]*light*(2/255))
    terrain[solid]=0
    mark("RADAR_TEX",len(textures))
    texture(Image.fromarray(terrain.astype(np.uint8)),(512,512),False)
    mark("RADAR_ICONS",len(textures))
    texture(Image.open(packages/"misc/radaricons.png"),(128,128),False)
    mark("RADAR_RING",len(textures))
    texture(Image.open(packages/"misc/compass-base.png"),(256,256),False)
    mark("RADAR_COMPASS",len(textures))
    texture(Image.open(packages/"misc/compass-rose.png"),(256,256),False)
    mark("BULLET_TEX",len(textures))
    # AssaultCube decal textures encode neutral at 128, shadows below and highlights above.
    gray=np.asarray(Image.open(packages/"misc/bullethole.png").convert("L")
                    .resize((64,64),Image.Resampling.LANCZOS)).astype(int)
    delta=2*(gray-128)
    # Remove the nearly neutral compression fringe before cropping. Each
    # projected texel can cover many pixels when the rifle touches a wall.
    delta[np.abs(delta)<=4]=0
    rows,cols=np.nonzero(delta)
    x0,x1=max(0,cols.min()-1),min(64,cols.max()+2)
    y0,y1=max(0,rows.min()-1),min(64,rows.max()+2)
    mark("HOLE_LEFT",float(x0/64-.5))
    mark("HOLE_RIGHT",float(x1/64-.5))
    mark("HOLE_TOP",float(y0/64-.5))
    mark("HOLE_BOTTOM",float(y1/64-.5))
    delta=delta[y0:y1,x0:x1]
    decal=np.empty((*delta.shape,4),np.uint8)
    decal[:,:,:3]=np.maximum(delta,0)[:,:,None]
    decal[:,:,3]=np.minimum(255,np.maximum(-delta,0))
    # Store straight RGBA independently: bright pixels have RGB but zero alpha.
    decal=np.stack([np.asarray(Image.fromarray(ch).resize((64,64),Image.Resampling.BILINEAR))
                    for ch in decal.transpose(2,0,1)],axis=2)
    texture(Image.fromarray(decal),(64,64))
    mark("SPARK_TEX",len(textures))
    texture(Image.open(packages/"misc/base.png"),(32,32),False)
    mark("BLOOD_TEX",len(textures))
    texture(Image.open(packages/"misc/blood.png"),(256,256))
    mark("FLASH_TEX",len(textures))
    texture(Image.open(packages/"misc/muzzleflash.jpg"),(128,128),False)
    mark("DAMAGE_TEX",len(textures))
    texture(Image.open(packages/"misc/damage.png"),(256,256),False)
    mark("EQUIPMENT_TEX",len(textures))
    texture(icons,(256,256),False)
    mark("CROSSHAIR_TEX",len(textures))
    texture(Image.open(packages/"crosshairs/default.png"),(32,32),False)
    mark("DIGITS_TEX",len(textures))
    digits=Image.new("RGBA",(512,64))
    digits.paste(Image.open(packages/"misc/huddigits.png").convert("RGBA"))
    texture(digits,(512,64),False)
    mark("SOLID_TEX",len(textures))
    texture(Image.new("RGBA",(4,4),"white"),(4,4),False)
    mark("FONT_TEX",len(textures))
    texture(Image.open(packages/"misc/chars_default.png"),(512,512),False)
    mark("WORLD_LIGHT_TEX",len(textures))
    texture(Image.fromarray(lighting),(256,256),False)
    glyphs=re.findall(r"^fontchar\s+(-?\d+)\s+(-?\d+)\s+(\d+)\s+(\d+)",
                      (packages.parent/"config/font_default.cfg").read_text(),re.M)[:94]
    mark("VERTEX_BYTES",len(vertices))
    (out/"gpu_textures.bin").write_bytes(blob)
    (out/"gpu_vertices.bin").write_bytes(vertices)
    def rows(name,kind,data):
        return f"static const {kind} {name}[] = {{\n"+ "".join("    {"+",".join(map(str,r))+"},\n" for r in data)+"};\n"
    (out/"gpu.h").write_text("".join(symbols)+rows("images","ImageInfo",textures)+rows("batches","Batch",batches)+rows("glyphs","Glyph",glyphs))
