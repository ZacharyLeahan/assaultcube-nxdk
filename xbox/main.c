#include <SDL.h>
#include <hal/debug.h>
#include <hal/video.h>
#include <hal/xbox.h>
#include <pbkit/pbkit.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "generated/starts.h"

enum { W = 320, H = 240, FOCAL = 180, MAP = 256 };
typedef struct {
    uint8_t kind;
    int8_t floor, ceil;
    uint8_t wall, ground, roof, upper, tag;
} Cube;
extern const Cube world[MAP][MAP];
extern const uint32_t atlas[][4096], sky[128][256], target[80][48], rifle[72][100];
extern const uint8_t shot_audio[] __asm__("_auto"), shot_end[] __asm__("_auto_end");
extern const uint8_t auto_reload[], auto_reload_end[];
static const Cube outside = {0};
static uint32_t frame[H][W];
static float depth[H][W], row_distance[H];
static float px, py, eye, angle, pitch, tx, ty;
static int horizon, ammo, hits, health, spawn, aimed;
static unsigned cooldown, reload, flash, hitflash;
static SDL_AudioDeviceID audio_device;

static const Cube *cell(int x, int y) {
    return (unsigned)x < MAP && (unsigned)y < MAP ? &world[y][x] : &outside;
}
static int solid(const Cube *c) { return c->kind < 2; }
static int passable(float x, float y, float floor) {
    for (int i = 0; i < 4; ++i) {
        const Cube *c = cell((int)(x + ((i&1) ? .32f : -.32f)),
                             (int)(y + ((i&2) ? .32f : -.32f)));
        if (solid(c) || (c->tag & 192) || c->floor > floor + 1.5f ||
            c->ceil - fmaxf(c->floor, floor) < 4.5f) return 0;
    }
    return 1;
}

// Place the range target along a clear line, preferring the player's facing.
static void place_target(float desired) {
    float best = -1;
    for (int i = 0; i < 32; ++i) {
        int turn = (i & 1) ? -(i+1)/2 : i/2;
        float a = desired + turn * .19635f, cx = cosf(a), cy = sinf(a);
        float floor = cell((int)px,(int)py)->floor;
        float d;
        for (d = .5f; d <= 16; d += .5f) {
            float x = px + d*cx, y = py + d*cy;
            if (!passable(x, y, floor) || fabsf(cell((int)x,(int)y)->floor - (eye-3.6f)) > 2) break;
            floor = cell((int)x,(int)y)->floor;
        }
        float score = d - .08f*i;
        if (score > best) {
            best = score;
            tx = px + (d-.5f)*cx;
            ty = py + (d-.5f)*cy;
        }
        if (d > 16) break;
    }
    health = 100;
}

static void restart(void) {
    px = starts[spawn][0]+.5f; py = starts[spawn][1]+.5f;
    eye = cell((int)px,(int)py)->floor + 3.6f;
    angle = (starts[spawn][2]*.1f-90)*.017453293f;
    pitch = 0; ammo = 20; hits = 0;
    cooldown = reload = flash = hitflash = 0;
    place_target(angle);
    angle = atan2f(ty-py, tx-px);
}

static int clamp(int a, int lo, int hi) { return a < lo ? lo : a > hi ? hi : a; }

static void plane(int x, int lo, int hi, float z, int tex, int ceiling, float dx, float dy) {
    for (int y = lo; y < hi; ++y) {
        float dist = (eye-z)*row_distance[y];
        depth[y][x] = dist;
        if (ceiling && !tex) {
            depth[y][x] = 10000;
            continue;
        }
        int u = (int)((px+dx*dist)*8) & 63, v = (int)((py+dy*dist)*8) & 63;
        frame[y][x] = atlas[tex][v*64+u];
    }
}

static void wall(int x, int lo, int hi, float d, int tex, int u, int side) {
    float v = (eye + (horizon-lo-.5f)*d/FOCAL)*8, step = d*(8.f/FOCAL);
    for (int y = lo; y < hi; ++y, v -= step) {
        uint32_t color = atlas[tex][((-(int)v)&63)*64 + u];
        if (side) color = ((color >> 1) & 0x7f7f7f) + ((color >> 2) & 0x3f3f3f);
        frame[y][x] = color;
        depth[y][x] = d;
    }
}

// Traverse grid boundaries front-to-back, clipping against each floor and lintel.
static void column(int x, float dx, float dy) {
    int mx = (int)px, my = (int)py, sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
    float ix = 1/fmaxf(fabsf(dx), .00001f), iy = 1/fmaxf(fabsf(dy), .00001f);
    float nx = (sx > 0 ? mx+1-px : px-mx)*ix, ny = (sy > 0 ? my+1-py : py-my)*iy;
    int top = 0, bottom = H;
    const Cube *c = cell(mx,my);
    for (int n = 0; n < 200 && top < bottom; ++n) {
        int side = nx >= ny;
        float d;
        if (side) { d = ny; ny += iy; my += sy; }
        else { d = nx; nx += ix; mx += sx; }
        d = fmaxf(d,.005f);
        float scale = FOCAL/d;
        int floor_y = clamp((int)(horizon+(eye-c->floor)*scale+.5f),top,bottom);
        plane(x,floor_y,bottom,c->floor,c->ground,0,dx,dy);
        bottom = floor_y;
        int ceil_y = clamp((int)(horizon+(eye-c->ceil)*scale+.5f),top,bottom);
        plane(x,top,ceil_y,c->ceil,c->roof,1,dx,dy);
        top = ceil_y;
        const Cube *next = cell(mx,my);
        int u = (int)((side ? px+dx*d : py+dy*d)*8)&63;
        if (solid(next)) {
            wall(x,top,bottom,d,next->wall,u,side);
            break;
        }
        int ledge = clamp((int)(horizon+(eye-next->floor)*scale+.5f),top,bottom);
        wall(x,ledge,bottom,d,next->wall,u,side);
        bottom = ledge;
        int lintel = clamp((int)(horizon+(eye-next->ceil)*scale+.5f),top,bottom);
        wall(x,top,lintel,d,next->upper,u,side);
        top = lintel;
        c = next;
    }
}

static void draw_scene(void) {
    float cx = cosf(angle), cy = sinf(angle);
    horizon = H/2+(int)pitch;
    int sky_offset = (int)(angle*163);
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
        column(x,cx-k*cy,cy+k*cx);
    }
    aimed = 0;
    float dx = tx-px, dy = ty-py, dist = dx*cx+dy*cy;
    if (dist > .3f) {
        float scale = FOCAL/dist;
        float width = 2.5f*scale, height = 4.6f*scale;
        float left = W/2 + (dy*cx-dx*cy)*scale - width/2;
        float top = horizon + (eye-cell((int)tx,(int)ty)->floor)*scale-height;
        for (int y = clamp((int)ceilf(top),0,H); y < H && y < top+height; ++y)
            for (int x = clamp((int)ceilf(left),0,W); x < W && x < left+width; ++x) {
                uint32_t color = target[clamp((int)((y-top)*80/height),0,79)][clamp((int)((x-left)*48/width),0,47)];
                if ((color>>24) && dist < depth[y][x]) {
                    frame[y][x] = color;
                    if (x == W/2 && y == H/2) aimed = 1;
                }
            }
    }
}

static void sound(const uint8_t *begin, const uint8_t *end) {
    if (!audio_device) return;
    SDL_ClearQueuedAudio(audio_device);
    SDL_QueueAudio(audio_device, begin, (Uint32)((uintptr_t)end-(uintptr_t)begin));
}

static void draw_hud(unsigned now) {
    int shift = reload ? 27 : flash ? 4 : 0;
    for (int y = 0; y < 72; ++y) {
        int row = H-60+y+shift;
        if (row >= H) break;
        for (int x = 0; x < 100; ++x) {
            uint32_t color = rifle[y][99-x];
            if (color>>24) frame[row][W/2+12+x] = color;
        }
    }
    uint32_t ink = hitflash ? 0xff6438 : aimed ? 0xffda70 : 0xf3f3e8;
    for (int i = 3; i <= 6; ++i) {
        frame[H/2][W/2-i] = frame[H/2][W/2+i] = ink;
        frame[H/2-i][W/2] = frame[H/2+i][W/2] = ink;
    }
    (void)now;
}

static float axis(SDL_GameController *pad, SDL_GameControllerAxis a) {
    float v = SDL_GameControllerGetAxis(pad,a)/32768.f;
    return fabsf(v) < .18f ? 0 : copysignf((fabsf(v)-.18f)/.82f,v);
}

int main(void) {
    XVideoSetMode(640,480,32,REFRESH_DEFAULT);
    if (SDL_Init(SDL_INIT_GAMECONTROLLER) || pb_init()) { debugPrint("AssaultCube init failed"); return 1; }
    pb_show_front_screen();
    if (SDL_InitSubSystem(SDL_INIT_AUDIO)==0) {
        SDL_AudioSpec spec = {0};
        spec.freq = 48000; spec.format = AUDIO_S16LSB; spec.channels = 1; spec.samples = 1024;
        audio_device = SDL_OpenAudioDevice(NULL,0,&spec,NULL,0);
        if (audio_device) SDL_PauseAudioDevice(audio_device,0);
    }
    SDL_GameController *pads[4] = {0};
    unsigned last = SDL_GetTicks(), reset_time = 0;
    int previous_start = 0;
    restart();
    for (;;) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_CONTROLLERDEVICEADDED) {
                for (int i = 0; i < 4; ++i) if (!pads[i]) {
                    pads[i] = SDL_GameControllerOpen(event.cdevice.which);
                    break;
                }
            }
            if (event.type == SDL_CONTROLLERDEVICEREMOVED)
                for (int i = 0; i < 4; ++i) if (pads[i] && !SDL_GameControllerGetAttached(pads[i])) {
                    SDL_GameControllerClose(pads[i]); pads[i] = NULL;
                }
        }
        unsigned now = SDL_GetTicks(), elapsed = now-last;
        last = now;
        float dt = fminf(elapsed*.001f,.075f);
        float move = 0, strafe = 0, turn = 0, look = 0;
        int fire = 0, refill = 0, start = 0, reset = 0, connected = 0;
        for (int i = 0; i < 4; ++i) if (pads[i]) {
            SDL_GameController *p = pads[i];
            connected = 1;
            move -= axis(p,SDL_CONTROLLER_AXIS_LEFTY); strafe += axis(p,SDL_CONTROLLER_AXIS_LEFTX);
            turn += axis(p,SDL_CONTROLLER_AXIS_RIGHTX); look -= axis(p,SDL_CONTROLLER_AXIS_RIGHTY);
            int rt = axis(p,SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > .25f;
            fire |= rt; refill |= SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_X);
            start |= SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_START);
            reset |= rt && axis(p,SDL_CONTROLLER_AXIS_TRIGGERLEFT) > .5f &&
                     SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_BACK) &&
                     SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
        }
        reset_time = reset ? reset_time+elapsed : 0;
        if (reset_time >= 1000) XReboot();
        if (start && !previous_start) { spawn = (spawn+1) % (sizeof(starts)/sizeof(starts[0])); restart(); }
        previous_start = start;
        angle = fmodf(angle + turn*2.6f*dt,6.2831853f);
        pitch = fmaxf(-75,fminf(75,pitch + look*130*dt));
        float speed = 16*dt/fmaxf(1,sqrtf(move*move+strafe*strafe));
        float nx = px+(move*cosf(angle)-strafe*sinf(angle))*speed;
        float ny = py+(move*sinf(angle)+strafe*cosf(angle))*speed;
        if (passable(nx,py,cell((int)px,(int)py)->floor)) px = nx;
        if (passable(px,ny,cell((int)px,(int)py)->floor)) py = ny;
        eye = cell((int)px,(int)py)->floor+3.6f;
        cooldown = elapsed >= cooldown ? 0 : cooldown-elapsed;
        flash = elapsed >= flash ? 0 : flash-elapsed;
        hitflash = elapsed >= hitflash ? 0 : hitflash-elapsed;
        if (reload) {
            if (elapsed >= reload) { reload = 0; ammo = 20; }
            else reload -= elapsed;
        }
        draw_scene();
        if (!reload && ammo < 20 && (refill || (fire && !ammo))) {
            reload = 2000;
            sound(auto_reload,auto_reload_end);
        } else if (fire && !reload && !cooldown && ammo) {
            --ammo; cooldown = 120; flash = 80;
            sound(shot_audio,shot_end);
            if (aimed) {
                hitflash = 150; health -= 22;
                if (health <= 0) { ++hits; place_target(angle + ((hits&1) ? .6f : -.6f)); }
            }
        }
        draw_hud(now);
        pb_target_back_buffer();
        pb_reset();
        unsigned pitch_bytes = pb_back_buffer_pitch();
        uint8_t *back = (uint8_t *)pb_back_buffer();
        uint32_t row[W*2];
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) row[2*x] = row[2*x+1] = frame[y][x];
            memcpy(back+y*2*pitch_bytes,row,sizeof(row));
            memcpy(back+(y*2+1)*pitch_bytes,row,sizeof(row));
        }
        pb_erase_text_screen();
        pb_print("\n   ASSAULTCUBE  /  ac_desert\n   Range %d   MTP-57  %d/20  %s\n",hits,ammo,reload ? "RELOADING" : "");
        if (!connected) pb_print("   Connect an Xbox controller\n");
        pb_draw_text_screen();
        while (pb_busy()) {}
        while (pb_finished()) {}
    }
}
