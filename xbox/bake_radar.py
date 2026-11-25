"""Rasterize the static map models into the overhead map at bake time."""
import numpy as np
from PIL import Image


def sample_floors(directory, names, slots, px, py, span):
    pixels_per_unit = len(px) / span
    result = np.zeros((*slots.shape, 3), dtype=float)
    for slot in np.unique(slots):
        mask = slots == slot
        with Image.open(directory / names[slot].decode()) as source:
            # One repeat covers width/32 by height/32 map units on the PC.
            size = tuple(max(1, round(n * pixels_per_unit / 32)) for n in source.size)
            tile = np.asarray(source.convert("RGB").resize(size, Image.Resampling.LANCZOS))
            u = px[mask] * (size[0]*32/source.width) - .5
            v = py[mask] * (size[1]*32/source.height) - .5
        x, y = np.floor(u).astype(int), np.floor(v).astype(int)
        fx, fy = (u-x)[:, None], (v-y)[:, None]
        top = (1-fx)*tile[y % size[1], x % size[0]] + fx*tile[y % size[1], (x+1) % size[0]]
        bottom = (1-fx)*tile[(y+1) % size[1], x % size[0]] + fx*tile[(y+1) % size[1], (x+1) % size[0]]
        result[mask] = (1-fy)*top + fy*bottom
    return result


def draw_models(image, depth, meshes, left, top, span):
    height, width = depth.shape
    for positions, uv, faces, skin, light in meshes:
        screen = (positions[:, :2] - (left, top)) * (width / span)
        for face in faces:
            a, b, c = screen[face]
            ab, ac = b - a, c - a
            determinant = ab[0]*ac[1] - ab[1]*ac[0]
            if abs(determinant) < 1e-8:
                continue
            x0, y0 = np.maximum(0, np.ceil(np.minimum(a, np.minimum(b, c)) - .5)).astype(int)
            x1, y1 = np.minimum((width, height), np.floor(np.maximum(a, np.maximum(b, c)) - .5) + 1).astype(int)
            if x1 <= x0 or y1 <= y0:
                continue
            y, x = np.ogrid[y0:y1, x0:x1]
            x, y = x + .5 - a[0], y + .5 - a[1]
            u = (x*ac[1] - y*ac[0]) / determinant
            v = (ab[0]*y - ab[1]*x) / determinant
            w = 1 - u - v
            z = positions[face, 2]
            surface = w*z[0] + u*z[1] + v*z[2]
            previous = depth[y0:y1, x0:x1]
            mask = (w >= 0) & (u >= 0) & (v >= 0) & (surface >= previous)
            if not mask.any():
                continue
            previous[mask] = surface[mask]
            coords = np.stack((w[mask], u[mask], v[mask]), axis=1) @ uv[face]
            tx, ty = np.floor((coords % 1) * (skin.shape[1], skin.shape[0])).astype(int).T
            image[y0:y1, x0:x1][mask] = np.minimum(255, skin[ty, tx] * (light * (2/255)))
