#include "match.h"
#include "navigation.h"
#include <math.h>
#include <string.h>

extern const Cube world[MAP][MAP];
const Cube *map_cube(int x, int y) {
    static const Cube outside = {0};
    return (unsigned)x < MAP && (unsigned)y < MAP ? &world[y][x] : &outside;
}
int map_solid(const Cube *cube) { return cube->kind < 2; }

static int fits(const Cube *c, float previous) {
    return !map_solid(c) && !(c->tag & 192) && c->floor <= previous + 1.5f &&
           c->ceil - fmaxf(c->floor, previous) >= 4.5f;
}
static int passable(float x, float y, float previous) {
    if (!fits(map_cube((int)x,(int)y),previous)) return 0;
    for (int i = 0; i < 4; ++i) {
        const Cube *corner = map_cube((int)(x + ((i&1) ? .32f : -.32f)),
                                      (int)(y + ((i&2) ? .32f : -.32f)));
        if (map_solid(corner) || (corner->tag & 192)) return 0;
    }
    return 1;
}
void walk(float *x, float *y, float dx, float dy) {
    // Small steps also check intervening cells during a slow frame.
    int steps = 1 + (int)(fmaxf(fabsf(dx), fabsf(dy)) * 3);
    for (int i = 0; i < steps; ++i) {
        float nx = *x+dx/steps, ny = *y+dy/steps;
        if (passable(nx,*y,map_cube((int)*x,(int)*y)->floor)) *x = nx;
        if (passable(*x,ny,map_cube((int)*x,(int)*y)->floor)) *y = ny;
    }
}
int visible(float x, float y, float z, float tx, float ty, float tz) {
    float dx = tx-x, dy = ty-y;
    // Visit each boundary so thin pillars and raised ledges occlude gunfire.
    int ix = (int)x, iy = (int)y, sx = dx >= 0 ? 1 : -1, sy = dy >= 0 ? 1 : -1;
    float step_x = 1/fmaxf(fabsf(dx),.000001f), step_y = 1/fmaxf(fabsf(dy),.000001f);
    float nx = (sx>0 ? ix+1-x : x-ix)*step_x, ny = (sy>0 ? iy+1-y : y-iy)*step_y;
    float t = 0;
    for (;;) {
        float end = fminf(1,fminf(nx,ny));
        const Cube *c = map_cube(ix,iy);
        float a = z+(tz-z)*t, b = z+(tz-z)*end;
        if (map_solid(c) || fminf(a,b) <= c->floor || fmaxf(a,b) >= c->ceil) return 0;
        if (end >= 1) return 1;
        t = end;
        if (nx <= end) { ix += sx; nx += step_x; }
        if (ny <= end) { iy += sy; ny += step_y; }
    }
}

// A reverse breadth-first search gives the next walkable cell from any spawn.
// Directed edges allow drops while enforcing the same step height as movement.
static uint8_t next_step[MAP*MAP];
static uint16_t queue[MAP*MAP];
static const int vx[] = {1,-1,0,0}, vy[] = {0,0,1,-1};
static int goal_x = -1, goal_y = -1;
void route_to(int x, int y) {
    if (goal_x == x && goal_y == y) return;
    goal_x = x; goal_y = y;
    memset(next_step, 255, sizeof(next_step));
    int head = 0, tail = 0, id = y*MAP+x;
    queue[tail++] = (uint16_t)id;
    next_step[id] = 4;
    while (head < tail) {
        id = queue[head++];
        int cx = id % MAP, cy = id / MAP;
        const Cube *dest = map_cube(cx,cy);
        for (int i = 0; i < 4; ++i) {
            int px = cx-vx[i], py = cy-vy[i];
            const Cube *from = map_cube(px,py);
            if (map_solid(from) || !fits(from,from->floor) || !fits(dest,from->floor)) continue;
            int previous = py*MAP+px;
            if (next_step[previous] != 255) continue;
            next_step[previous] = (uint8_t)i;
            queue[tail++] = (uint16_t)previous;
        }
    }
}
int route_next(float x, float y, float *tx, float *ty) {
    int cx = (int)x, cy = (int)y, dir = next_step[cy*MAP+cx];
    if (dir == 255) return 0;
    *tx = cx+.5f; *ty = cy+.5f;
    if (dir < 4) {
        // Align with the corridor before turning around an inside corner.
        if (vx[dir] && fabsf(y-*ty) > .15f) *tx = x;
        else if (vy[dir] && fabsf(x-*tx) > .15f) *ty = y;
        else { *tx += vx[dir]; *ty += vy[dir]; }
    }
    return 1;
}
