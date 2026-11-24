#include "render.h"
#include <math.h>

enum { FOCAL = 180 };
extern const uint32_t atlas[][4096], sky[128][256], target[80][48], rifle[72][100];
static float depth[H][W], row_distance[H];
static int horizon;

static int clamp(int a, int lo, int hi) { return a < lo ? lo : a > hi ? hi : a; }

static void plane(const Range *r, RangeFrame frame, int x, int lo, int hi, float z, int tex, int ceiling, float dx, float dy) {
    for (int y = lo; y < hi; ++y) {
        float dist = (r->eye-z)*row_distance[y];
        depth[y][x] = dist;
        if (ceiling && !tex) {
            depth[y][x] = 10000;
            continue;
        }
        int u = (int)((r->px+dx*dist)*8) & 63, v = (int)((r->py+dy*dist)*8) & 63;
        frame[y][x] = atlas[tex][v*64+u];
    }
}

static void wall(const Range *r, RangeFrame frame, int x, int lo, int hi, float d, int tex, int u, int side) {
    float v = (r->eye + (horizon-lo-.5f)*d/FOCAL)*8, step = d*(8.f/FOCAL);
    for (int y = lo; y < hi; ++y, v -= step) {
        uint32_t color = atlas[tex][((-(int)v)&63)*64 + u];
        if (side) color = ((color >> 1) & 0x7f7f7f) + ((color >> 2) & 0x3f3f3f);
        frame[y][x] = color;
        depth[y][x] = d;
    }
}

// Traverse grid boundaries front-to-back, clipping against each floor and lintel.
static void column(const Range *r, RangeFrame frame, int x, float dx, float dy) {
    int mx = (int)r->px, my = (int)r->py, sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
    float ix = 1/fmaxf(fabsf(dx), .00001f), iy = 1/fmaxf(fabsf(dy), .00001f);
    float nx = (sx > 0 ? mx+1-r->px : r->px-mx)*ix, ny = (sy > 0 ? my+1-r->py : r->py-my)*iy;
    int top = 0, bottom = H;
    const Cube *c = range_cube_at(mx,my);
    for (int n = 0; n < 200 && top < bottom; ++n) {
        int side = nx >= ny;
        float d;
        if (side) { d = ny; ny += iy; my += sy; }
        else { d = nx; nx += ix; mx += sx; }
        d = fmaxf(d,.005f);
        float scale = FOCAL/d;
        int floor_y = clamp((int)(horizon+(r->eye-c->floor)*scale+.5f),top,bottom);
        plane(r,frame,x,floor_y,bottom,c->floor,c->ground,0,dx,dy);
        bottom = floor_y;
        int ceil_y = clamp((int)(horizon+(r->eye-c->ceil)*scale+.5f),top,bottom);
        plane(r,frame,x,top,ceil_y,c->ceil,c->roof,1,dx,dy);
        top = ceil_y;
        const Cube *next = range_cube_at(mx,my);
        int u = (int)((side ? r->px+dx*d : r->py+dy*d)*8)&63;
        if (range_cube_is_solid(next)) {
            wall(r,frame,x,top,bottom,d,next->wall,u,side);
            break;
        }
        int ledge = clamp((int)(horizon+(r->eye-next->floor)*scale+.5f),top,bottom);
        wall(r,frame,x,ledge,bottom,d,next->wall,u,side);
        bottom = ledge;
        int lintel = clamp((int)(horizon+(r->eye-next->ceil)*scale+.5f),top,bottom);
        wall(r,frame,x,top,lintel,d,next->upper,u,side);
        top = lintel;
        c = next;
    }
}

int render_scene(const Range *r, RangeFrame frame) {
    float cx = cosf(r->angle), cy = sinf(r->angle);
    horizon = H/2+(int)r->pitch;
    int sky_offset = (int)(r->angle*163);
    for (int y = 0; y < H; ++y) {
        row_distance[y] = FOCAL/(y+.5f-horizon);
        int v = clamp(80+(y-horizon)/2,0,127);
        for (int x = 0; x < W; ++x) {
            frame[y][x] = sky[v][(sky_offset+x/2)&255];
            depth[y][x] = 10000;
        }
    }
    for (int x = 0; x < W; ++x) {
        float k = (x+.5f-W/2)/FOCAL;
        column(r,frame,x,cx-k*cy,cy+k*cx);
    }
    int aimed = 0;
    float dx = r->tx-r->px, dy = r->ty-r->py, dist = dx*cx+dy*cy;
    if (dist > .3f) {
        float scale = FOCAL/dist;
        float width = 2.5f*scale, height = 4.6f*scale;
        float left = W/2 + (dy*cx-dx*cy)*scale - width/2;
        float top = horizon + (r->eye-range_cube_at((int)r->tx,(int)r->ty)->floor)*scale-height;
        for (int y = clamp((int)ceilf(top),0,H); y < H && y < top+height; ++y)
            for (int x = clamp((int)ceilf(left),0,W); x < W && x < left+width; ++x) {
                uint32_t color = target[clamp((int)((y-top)*80/height),0,79)][clamp((int)((x-left)*48/width),0,47)];
                if ((color>>24) && dist < depth[y][x]) {
                    frame[y][x] = color;
                    if (x == W/2 && y == H/2) aimed = 1;
                }
            }
    }
    return aimed;
}

void render_overlay(const Range *r, int aimed, RangeFrame frame) {
    int shift = r->reload ? 27 : r->flash ? 4 : 0;
    for (int y = 0; y < 72; ++y) {
        int row = H-60+y+shift;
        if (row >= H) break;
        for (int x = 0; x < 100; ++x) {
            uint32_t color = rifle[y][99-x];
            if (color>>24) frame[row][W/2+12+x] = color;
        }
    }
    uint32_t ink = r->hitflash ? 0xff6438 : aimed ? 0xffda70 : 0xf3f3e8;
    for (int i = 3; i <= 6; ++i) {
        frame[H/2][W/2-i] = frame[H/2][W/2+i] = ink;
        frame[H/2-i][W/2] = frame[H/2+i][W/2] = ink;
    }
}
