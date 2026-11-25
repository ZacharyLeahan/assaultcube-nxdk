"""Resolve the original map's mip-sized corner cubes for geometry and collisions."""
import numpy as np


def map_corners(cells, deltas):
    world = np.frombuffer(cells, np.uint8).reshape(256, 256, 8).astype(int)
    world[:, :, 1:3] = (world[:, :, 1:3] + 128) % 256 - 128
    delta = np.frombuffer(deltas, np.uint8).reshape(256, 256)
    corners = {}
    size = 1
    while np.any(world[:, :, 0] == 1):
        # remip inherits materials from the top left child and encloses all
        # non-solid children, including the PC's extra heightfield allowance.
        children = np.stack([world[dy::2, dx::2] for dy in range(2) for dx in range(2)])
        heights = np.stack([delta[dy::2, dx::2] for dy in range(2) for dx in range(2)]) // 4 + 2
        parent = children[0].copy()
        kinds = children[:, :, :, 0]
        mixed = np.any(kinds != kinds[0], axis=0)
        parent[:, :, 0][mixed] = 5
        parent[:, :, 1] = np.where(kinds == 0, 127, children[:, :, :, 1] -
                                   heights * ((kinds == 2) & mixed)).min(axis=0)
        parent[:, :, 2] = np.where(kinds == 0, -128, children[:, :, :, 2] +
                                   heights * ((kinds == 3) & mixed)).max(axis=0)
        for y, x in zip(*np.nonzero(world[:, :, 0] == 1)):
            if parent[y//2, x//2, 0] == 1:
                continue
            left, right = world[y, x-1], world[y, x+1]
            top, bottom = world[y-1, x], world[y+1, x]
            cell = world[y, x]
            if left[0] == 0 or right[0] == 0:
                if top[0] and bottom[0]:
                    raise ValueError(f"Invalid corner at {x*size}, {y*size}")
                direction = (2 if top[0] == 0 else 3) if left[0] == 0 else int(top[0] == 0)
                inside, outside = (top if top[0] == 0 else bottom), cell
            else:
                top_narrow = top[2]-top[1] < bottom[2]-bottom[1]
                left_narrow = left[2]-left[1] < right[2]-right[1]
                direction = (2 if top_narrow else 3) if left_narrow else int(top_narrow)
                inside, outside = cell, (bottom if top_narrow else top)
            corners[int(x*size), int(y*size)] = (size, direction, inside, outside)
        world, delta, size = parent, delta[::2, ::2], size*2
    return corners
