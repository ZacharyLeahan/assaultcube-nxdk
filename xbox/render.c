#include "render.h"
#include <math.h>

enum { FOCAL = 180 };
extern const uint32_t atlas[][4096], sky[128][256], target[80][48], rifle[72][100];
extern const uint32_t poses[4][80][48], items[5][32][32], pistol[72][100];
static float depth[H][W], row_distance[H];
static int horizon;

static int clamp(int a, int lo, int hi) { return a < lo ? lo : a > hi ? hi : a; }

static void plane(const MatchView *r, MatchFrame frame, int x, int lo, int hi, float z, int tex, int ceiling, float dx, float dy) {
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

static void wall(const MatchView *r, MatchFrame frame, int x, int lo, int hi, float d, int tex, int u, int side) {
    float v = (r->eye + (horizon-lo-.5f)*d/FOCAL)*8, step = d*(8.f/FOCAL);
    for (int y = lo; y < hi; ++y, v -= step) {
        uint32_t color = atlas[tex][((-(int)v)&63)*64 + u];
        if (side) color = ((color >> 1) & 0x7f7f7f) + ((color >> 2) & 0x3f3f3f);
        frame[y][x] = color;
        depth[y][x] = d;
    }
}

// Traverse grid boundaries front-to-back, clipping against each floor and lintel.
static void column(const MatchView *r, MatchFrame frame, int x, float dx, float dy) {
    int mx = (int)r->px, my = (int)r->py, sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
    float ix = 1/fmaxf(fabsf(dx), .00001f), iy = 1/fmaxf(fabsf(dy), .00001f);
    float nx = (sx > 0 ? mx+1-r->px : r->px-mx)*ix, ny = (sy > 0 ? my+1-r->py : r->py-my)*iy;
    int top = 0, bottom = H;
    const Cube *c = map_cube(mx,my);
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
        const Cube *next = map_cube(mx,my);
        int u = (int)((side ? r->px+dx*d : r->py+dy*d)*8)&63;
        if (map_solid(next)) {
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

static int sprite(const MatchView *r, MatchFrame frame, float cx, float cy, float wx, float wy,
                  float world_w, float world_h, const uint32_t *image, int sw, int sh) {
    int aimed = 0;
    float dx = wx-r->px, dy = wy-r->py, dist = dx*cx+dy*cy;
    if (dist > .3f) {
        float scale = FOCAL/dist;
        float width = world_w*scale, height = world_h*scale;
        float left = W/2 + (dy*cx-dx*cy)*scale - width/2;
        float top = horizon + (r->eye-map_cube((int)wx,(int)wy)->floor)*scale-height;
        for (int y = clamp((int)ceilf(top),0,H); y < H && y < top+height; ++y)
            for (int x = clamp((int)ceilf(left),0,W); x < W && x < left+width; ++x) {
                int v = clamp((int)((y-top)*sh/height),0,sh-1);
                int u = clamp((int)((x-left)*sw/width),0,sw-1);
                uint32_t color = image[v*sw+u];
                if ((color>>24) && dist < depth[y][x]) {
                    frame[y][x] = color;
                    depth[y][x] = dist;
                    if (x == W/2 && y == H/2) aimed = 1;
                }
            }
    }
    return aimed;
}

int render_scene(const MatchView *r, MatchFrame frame) {
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
    const uint32_t *skin = r->bot_pose ? &poses[r->bot_pose-1][0][0] : &target[0][0];
    int aimed = r->bot_alive && sprite(r,frame,cx,cy,r->tx,r->ty,2.5f,4.6f,skin,48,80);
    for (int i = 0; i < r->item_count; ++i) {
        const ItemView *item = &r->items[i];
        sprite(r,frame,cx,cy,item->x+.5f,item->y+.5f,1.3f,1.3f,&items[item->icon][0][0],32,32);
    }
    return aimed;
}

void render_overlay(const MatchView *r, int aimed, MatchFrame frame) {
    if (r->respawning) return;
    int shift = r->reload ? 27 : r->flash ? 4 : 0;
    for (int y = 0; y < 72; ++y) {
        int row = H-60+y+shift;
        if (row >= H) break;
        for (int x = 0; x < 100; ++x) {
            uint32_t color = r->pistol ? pistol[y][99-x] : rifle[y][99-x];
            if (color>>24) frame[row][W/2+12+x] = color;
        }
    }
    uint32_t ink = r->hitflash ? 0xff6438 : aimed ? 0xffda70 : 0xf3f3e8;
    for (int i = 3; i <= 6; ++i) {
        frame[H/2][W/2-i] = frame[H/2][W/2+i] = ink;
        frame[H/2-i][W/2] = frame[H/2+i][W/2] = ink;
    }
}
