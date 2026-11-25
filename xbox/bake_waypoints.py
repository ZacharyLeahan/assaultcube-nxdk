"""Pack the bundled Cube Bot v2 graph and shortest paths to ac_desert pickups."""
import heapq
import math
import struct


def bake_waypoints(path, output, pickups, cells):
    data = path.read_bytes()
    magic, version, flags, count, mapname = struct.unpack_from("<10s2xiii32s", data)
    if magic.rstrip(b"\0") != b"cube_bot" or version != 2 or mapname.rstrip(b"\0") != b"ac_desert":
        raise ValueError("Expected the ac_desert Cube Bot version 2 waypoints")
    pos = 56 + count * 20
    nodes = list(struct.iter_unpack("<3fihh", data[56:pos]))
    index = {node[:3]: i for i, node in enumerate(nodes)}
    links = [[] for _ in nodes]
    reverse = [[] for _ in nodes]
    for _ in nodes:
        size, *origin = struct.unpack_from("<h3f", data, pos)
        pos += 14
        i = index[tuple(origin)]
        for slot in range(size):
            target = struct.unpack_from("<3f", data, pos)
            pos += 12
            j = index[target]
            links[i].append(j)
            reverse[j].append((i, slot, math.dist(origin, target)))
    if pos != len(data) or len(index) != count or any(len(row) >= 254 for row in links):
        raise ValueError("Invalid waypoint graph")

    # One byte identifies an outgoing link. The static map and its pickup sites
    # never move, so route searches need no time or scratch space during play.
    choices = []
    for x, y, height, _, _ in pickups:
        floor = struct.unpack_from("<b", cells, 8*(y*256+x)+1)[0]
        target = (x, y, floor + height/10 + 4.5)
        goal = min(range(count), key=lambda i: math.dist(target, nodes[i][:3]))
        row = [255] * count
        row[goal] = 254
        costs = [math.inf] * count
        costs[goal] = 0
        pending = [(0, goal)]
        while pending:
            cost, j = heapq.heappop(pending)
            if cost != costs[j]:
                continue
            for i, slot, length in reverse[j]:
                candidate = cost + length
                if candidate < costs[i]:
                    costs[i], row[i] = candidate, slot
                    heapq.heappush(pending, (candidate, i))
        choices.append(row)

    first = 0
    records = []
    for (x, y, z, flags, _, _), edges in zip(nodes, links):
        records.append(f"    {{{x}f,{y}f,{z}f,{first},{len(edges)},{flags}}}")
        first += len(edges)
    output.write_text("static const Waypoint waypoints[]={\n" + ",\n".join(records) +
                      "\n};\nstatic const unsigned short links[]={\n" +
                      ",\n".join(",".join(map(str, edges)) for edges in links if edges) +
                      "\n};\nstatic const unsigned char pickup_links[][" + str(count) + "]={\n" +
                      ",\n".join("{"+",".join(map(str, row))+"}" for row in choices) + "\n};\n")
