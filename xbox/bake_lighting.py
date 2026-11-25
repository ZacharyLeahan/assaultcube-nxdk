"""Bake the PC world's static model lighting into the Xbox asset pack."""
import numpy as np


def model_lighting(cells, entities, revision, ambient):
    # Mirrors calclight/lightray in source/src/worldlight.cpp. Cast in XY even
    # when the light entity sits well above the floor. Solid cells stop rays.
    size = 256
    kinds = np.frombuffer(cells, np.uint8).reshape(size, size, 8)[:, :, 0]
    base = (ambient >> 16, (ambient >> 8) & 255, ambient & 255)
    if not any(base[:2]):
        base = (base[2] or 10,) * 3
    rgb = np.empty((size, size, 3), dtype=np.int32)
    rgb[:] = base
    rng = np.random.RandomState(len(entities) + revision)

    def rnd(limit):
        return (int(rng.randint(0, 2**32, dtype=np.uint32)) & 0xffffff) % limit

    def ray(bx, by):
        lx = np.float32(x + (rnd(21) - 10) * .1)
        ly = np.float32(y + (rnd(21) - 10) * .1)
        dx, dy = bx - lx, by - ly
        distance = np.sqrt(dx * dx + dy * dy)
        if distance < 1:
            return
        steps = int(np.float32(radius * radius * 1.6) / distance)
        sx, sy = int(dx / steps * 4096), int(dy / steps * 4096)
        jitter = rnd((255 - sum(channels) // 3) // 16 + 1)
        fx, fy = int(lx * 4096) + jitter * sx, int(ly * 4096) + jitter * sy
        if not (2 <= fx >> 12 < size-2 and 2 <= fy >> 12 < size-2):
            return
        power = [v * 1024 for v in channels]
        falloff = [int(v * 4096 / steps) // 4 for v in channels]
        for _ in range(steps):
            cx, cy = fx >> 12, fy >> 12
            pixel = rgb[cy, cx]
            if white and ambient <= 255:
                pixel[:] = min(255, pixel[0] + (power[0] >> 12))
            else:
                for k in range(3):
                    pixel[k] = min(255, pixel[k] + (power[k] >> 12))
            if kinds[cy, cx] == 0:
                break
            fx += sx
            fy += sy
            for k in range(3):
                power[k] -= falloff[k]
                falloff[k] -= 25

    for x, y, _, radius, kind, red, green, blue in entities:
        if kind != 1 or radius <= 0:
            continue
        white = green == blue == 0
        channels = (red,) * 3 if white else (red, green, blue)
        edge = np.float32(x - radius)
        while edge <= x + radius:
            ray(edge, np.float32(y - radius))
            ray(edge, np.float32(y + radius))
            edge += np.float32(1.6)
        edge = np.float32(y - radius + .8)
        while edge <= y + radius - .8:
            ray(np.float32(x - radius), edge)
            ray(np.float32(x + radius), edge)
            edge += np.float32(1.6)

    # PC postlightarea filters alternating cells in place, one row at a time.
    ys, xs = np.nonzero(kinds)
    left, right = xs.min() - 1, xs.max() + 1
    top, bottom = ys.min() - 1, ys.max() + 1
    for last in (bottom, bottom - 1):
        for y in range(last, top - 1, -2):
            for start in (left, left + 1):
                x = np.arange(start, right + 1, 2)
                rgb[y, x] = (2 * (rgb[y, x] + rgb[y-1, x] + rgb[y+1, x]
                                  + rgb[y, x-1] + rgb[y, x+1])
                             + rgb[y-1, x-1] + rgb[y-1, x+1]
                             + rgb[y+1, x-1] + rgb[y+1, x+1]) // 14
    return rgb.astype(np.uint8)
