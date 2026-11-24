#include "range.h"
#include "generated/starts.h"
#include <math.h>

extern const Cube world[MAP][MAP];
static const Cube outside = {0};

const Cube *range_cube_at(int x, int y) {
    return (unsigned)x < MAP && (unsigned)y < MAP ? &world[y][x] : &outside;
}
int range_cube_is_solid(const Cube *c) { return c->kind < 2; }
static int passable(float x, float y, float floor) {
    for (int i = 0; i < 4; ++i) {
        const Cube *c = range_cube_at((int)(x + ((i&1) ? .32f : -.32f)),
                             (int)(y + ((i&2) ? .32f : -.32f)));
        if (range_cube_is_solid(c) || (c->tag & 192) || c->floor > floor + 1.5f ||
            c->ceil - fmaxf(c->floor, floor) < 4.5f) return 0;
    }
    return 1;
}

// Place the range target along a clear line, preferring the player's facing.
static void place_target(Range *r, float desired) {
    float best = -1;
    for (int i = 0; i < 32; ++i) {
        int turn = (i & 1) ? -(i+1)/2 : i/2;
        float a = desired + turn * .19635f, cx = cosf(a), cy = sinf(a);
        float floor = range_cube_at((int)r->px,(int)r->py)->floor;
        float d;
        for (d = .5f; d <= 16; d += .5f) {
            float x = r->px + d*cx, y = r->py + d*cy;
            if (!passable(x, y, floor) || fabsf(range_cube_at((int)x,(int)y)->floor - (r->eye-3.6f)) > 2) break;
            floor = range_cube_at((int)x,(int)y)->floor;
        }
        float score = d - .08f*i;
        if (score > best) {
            best = score;
            r->tx = r->px + (d-.5f)*cx;
            r->ty = r->py + (d-.5f)*cy;
        }
        if (d > 16) break;
    }
    r->health = 100;
}

void range_restart(Range *r) {
    r->px = starts[r->spawn][0]+.5f; r->py = starts[r->spawn][1]+.5f;
    r->eye = range_cube_at((int)r->px,(int)r->py)->floor + 3.6f;
    r->angle = (starts[r->spawn][2]*.1f-90)*.017453293f;
    r->pitch = 0; r->ammo = 20; r->hits = 0;
    r->cooldown = r->reload = r->flash = r->hitflash = 0;
    place_target(r, r->angle);
    r->angle = atan2f(r->ty-r->py, r->tx-r->px);
}

void range_move(Range *r, const RangeInput *input, unsigned elapsed) {
    if (input->start) {
        r->spawn = (r->spawn+1) % (sizeof(starts)/sizeof(starts[0]));
        range_restart(r);
    }
    float dt = fminf(elapsed*.001f,.075f);
    r->angle = fmodf(r->angle + input->turn*2.6f*dt,6.2831853f);
    r->pitch = fmaxf(-75,fminf(75,r->pitch + input->look*130*dt));
    float speed = 16*dt/fmaxf(1,sqrtf(input->move*input->move+input->strafe*input->strafe));
    float nx = r->px+(input->move*cosf(r->angle)-input->strafe*sinf(r->angle))*speed;
    float ny = r->py+(input->move*sinf(r->angle)+input->strafe*cosf(r->angle))*speed;
    if (passable(nx,r->py,range_cube_at((int)r->px,(int)r->py)->floor)) r->px = nx;
    if (passable(r->px,ny,range_cube_at((int)r->px,(int)r->py)->floor)) r->py = ny;
    r->eye = range_cube_at((int)r->px,(int)r->py)->floor+3.6f;
    r->cooldown = elapsed >= r->cooldown ? 0 : r->cooldown-elapsed;
    r->flash = elapsed >= r->flash ? 0 : r->flash-elapsed;
    r->hitflash = elapsed >= r->hitflash ? 0 : r->hitflash-elapsed;
    if (r->reload) {
        if (elapsed >= r->reload) { r->reload = 0; r->ammo = 20; }
        else r->reload -= elapsed;
    }
}

RangeSound range_fire(Range *r, const RangeInput *input, int aimed) {
    if (!r->reload && r->ammo < 20 && (input->refill || (input->fire && !r->ammo))) {
        r->reload = 2000;
        return RANGE_RELOAD;
    } else if (input->fire && !r->reload && !r->cooldown && r->ammo) {
        --r->ammo; r->cooldown = 120; r->flash = 80;

        if (aimed) {
            r->hitflash = 150; r->health -= 22;
            if (r->health <= 0) { ++r->hits; place_target(r, r->angle + ((r->hits&1) ? .6f : -.6f)); }
        }
        return RANGE_SHOT;
    }
    return RANGE_SILENT;
}
