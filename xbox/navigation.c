#include "match.h"
#include "navigation.h"
#include <math.h>
#include <string.h>

extern const Cube world[MAP][MAP];
extern const uint8_t deltas[MAP*MAP];
const Cube *map_cube(int x, int y) {
    static const Cube outside = {0};
    return (unsigned)x < MAP && (unsigned)y < MAP ? &world[y][x] : &outside;
}
int map_solid(const Cube *cube) { return cube->kind < 2; }

typedef struct {
    uint16_t index;
    int16_t plane;
    int8_t nx,ny,solid,inner_floor,inner_ceil,outer_floor,outer_ceil;
    uint8_t wall,inner_roof,outer_roof;
} MapCorner;
#include "generated/corners.h"

static const MapCorner *corner_at(int x,int y) {
    unsigned key=y*MAP+x,lo=0,hi=sizeof(corners)/sizeof(corners[0]);
    while (lo<hi) {
        unsigned mid=(lo+hi)/2;
        if (corners[mid].index<key) lo=mid+1;
        else hi=mid;
    }
    return &corners[lo]; // The baker emits an entry for each CORNER cell.
}

// The PC collision hull samples the average of the four heightfield vertices.
static float height_delta(const Cube *c) {
    size_t i=((uintptr_t)c-(uintptr_t)world)/sizeof(Cube);
    return (deltas[i]+deltas[i+1]+deltas[i+MAP]+deltas[i+MAP+1])/16.f;
}
float map_floor(const Cube *c) { return c->kind==2 ? c->floor-height_delta(c) : c->floor; }
static float map_ceiling(const Cube *c) { return c->kind==3 ? c->ceil+height_delta(c) : c->ceil; }

typedef struct { short x,y,offset,half_x,half_y,height,shape; } MapClip;
#include "generated/clips.h"

static void clip_space(float x,float y,float feet,float *floor,float *ceiling) {
    const float radius=.32f;
    for (unsigned i=0;i<sizeof(clips)/sizeof(clips[0]);++i) {
        const MapClip *clip=&clips[i];
        float dx=x-clip->x,dy=y-clip->y;
        float w=clip->half_x*.2f,h=clip->half_y*.2f;
        if (clip->shape==3) {
            float rr=radius*1.41421356f;
            float a=fabsf((dx-dy)*.70710678f)-h-rr;
            float b=fabsf((dx+dy)*.70710678f)-w-rr;
            if (a>=0 || b>=0 || a+b+rr>=0) continue;
        } else if (fabsf(dx)>w+radius+.00001f || fabsf(dy)>h+radius+.00001f) continue;
        float base=map_cube(clip->x,clip->y)->floor+clip->offset*.1f;
        if (feet<base-.42f) *ceiling=fminf(*ceiling,base);
        else *floor=fmaxf(*floor,base+clip->height*.2f);
    }
}

// The bot walks at terrain height and takes routes around raised obstacles.
static int bot_clip_clear(float x,float y,float feet) {
    float floor=feet,ceiling=127;
    clip_space(x,y,feet,&floor,&ceiling);
    return floor<=feet && ceiling>=feet+4.5f;
}

static int fits(const Cube *c, float previous) {
    return !map_solid(c) && !(c->tag & 192) && map_floor(c) <= previous + 1.5f &&
           map_ceiling(c) - fmaxf(map_floor(c), previous) >= 4.5f;
}
static int passable(float x, float y, float previous) {
    const Cube *dest=map_cube((int)x,(int)y);
    if (!fits(dest,previous) || !bot_clip_clear(x,y,map_floor(dest))) return 0;
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
        if (passable(nx,*y,map_floor(map_cube((int)*x,(int)*y)))) *x = nx;
        if (passable(*x,ny,map_floor(map_cube((int)*x,(int)*y)))) *y = ny;
    }
}
#include "generated/ladders.h"
int ladder_space(float x,float y,float feet,float *top) {
    for (unsigned i=0;i<sizeof(ladders)/sizeof(ladders[0]);++i) {
        const short *p=ladders[i];
        float dx=x-p[0],dy=y-p[1],end=map_cube(p[0],p[1])->floor+p[2];
        if (dx*dx+dy*dy<2.25f && feet<=end) { *top=end; return 1; }
    }
    return 0;
}
static const MapCorner *body_bounds(float x,float y,float feet,float *floor,float *ceiling) {
    const MapCorner *surface=NULL;
    *floor=-128; *ceiling=127;
    for (int i=0;i<4;++i) {
        float u=x+((i&1)?.32f:-.32f),v=y+((i&2)?.32f:-.32f);
        int ix=(int)u,iy=(int)v;
        const Cube *c=map_cube(ix,iy);
        if (c->kind==1) {
            const MapCorner *k=corner_at(ix,iy);
            int inner=k->nx*u+k->ny*v>=k->plane;
            if (inner) surface=k;
            if ((inner && k->solid) || ((inner || k->solid) && (c->tag&192))) {
                *floor=127; *ceiling=-128; return inner?k:NULL;
            }
            *floor=fmaxf(*floor,inner?k->inner_floor:k->outer_floor);
            *ceiling=fminf(*ceiling,inner?k->inner_ceil:k->outer_ceil);
            continue;
        }
        if (map_solid(c) || (c->tag&192)) { *floor=127; *ceiling=-128; return NULL; }
        *floor=fmaxf(*floor,map_floor(c)); *ceiling=fminf(*ceiling,map_ceiling(c));
    }
    clip_space(x,y,feet,floor,ceiling);
    return surface;
}
void body_space(float x,float y,float feet,float *floor,float *ceiling) {
    body_bounds(x,y,feet,floor,ceiling);
}
static int body_fits(float x,float y,float feet,float height,float step) {
    float floor,ceiling;
    body_space(x,y,feet,&floor,&ceiling);
    return floor<=feet+step && fmaxf(floor,feet)+height<=ceiling;
}
void walk_body(float *x,float *y,float dx,float dy,float feet,float height,float step) {
    int steps=1+(int)(fmaxf(fabsf(dx),fabsf(dy))*3);
    for (int i=0;i<steps;++i) {
        float sx=dx/steps,sy=dy/steps,nx=*x+sx,ny=*y+sy,floor,ceiling;
        const MapCorner *corner=body_bounds(nx,ny,feet,&floor,&ceiling);
        if (floor<=feet+step && fmaxf(floor,feet)+height<=ceiling) {
            *x=nx; *y=ny; continue;
        }
        if (corner) {
            // Project the displacement onto the diagonal and use the PC's
            // 1/42 backoff so the next update starts clear of the wall.
            float normal=(sx*corner->nx+sy*corner->ny)*.5f;
            float slide_x=nx-normal*corner->nx-sx/42;
            float slide_y=ny-normal*corner->ny-sy/42;
            if (body_fits(slide_x,slide_y,feet,height,step)) {
                *x=slide_x; *y=slide_y; continue;
            }
        }
        if (body_fits(nx,*y,feet,height,step)) *x=nx;
        if (body_fits(*x,ny,feet,height,step)) *y=ny;
    }
}
static float surface_delta(int ix,int iy,float u,float v,int ceiling) {
    const uint8_t *d=&deltas[iy*MAP+ix];
    float a=d[0], b=d[1], c=d[MAP+1], e=d[MAP];
    if (ceiling) {
        if (u+v<=1) return (a+u*(b-a)+v*(e-a))*.25f;
        return (c+(1-v)*(b-c)+(1-u)*(e-c))*.25f;
    }
    if (u>=v) return (a+u*(b-a)+v*(c-b))*.25f;
    return (a+v*(e-a)+u*(c-e))*.25f;
}
int trace_surface(float x,float y,float z,float tx,float ty,float tz,SurfaceHit *hit) {
    float dx = tx-x, dy = ty-y;
    // Visit each boundary so thin pillars and raised ledges occlude gunfire.
    int ix = (int)x, iy = (int)y, sx = dx >= 0 ? 1 : -1, sy = dy >= 0 ? 1 : -1;
    float step_x = 1/fmaxf(fabsf(dx),.000001f), step_y = 1/fmaxf(fabsf(dy),.000001f);
    float nx = (sx>0 ? ix+1-x : x-ix)*step_x, ny = (sy>0 ? iy+1-y : y-iy)*step_y;
    float t = 0;
    int entry_axis=0;
    for (;;) {
        float end = fminf(1,fminf(nx,ny));
        const Cube *c = map_cube(ix,iy);
        float boundary=end;
        int slope=c->kind==2 || c->kind==3;
        const MapCorner *k=c->kind==1?corner_at(ix,iy):NULL;
        int solid=c->kind==0,wall=c->wall,roof=c->roof;
        float fa=c->floor,ca=c->ceil;
        if (k) {
            float start=k->nx*x+k->ny*y-k->plane,rate=k->nx*dx+k->ny*dy;
            if (rate!=0) {
                float crossing=-start/rate;
                if (crossing>t && crossing<end) end=crossing;
            }
            int inner=start+rate*((t+end)*.5f)>=0;
            solid=inner && k->solid;
            wall=k->wall; roof=inner?k->inner_roof:k->outer_roof;
            fa=inner?k->inner_floor:k->outer_floor;
            ca=inner?k->inner_ceil:k->outer_ceil;
        }
        if (slope) {
            float rate=c->kind==2 ? dx-dy : dx+dy;
            if (rate!=0) {
                float crossing=(c->kind==2 ? ix-x+y-iy : 1+ix-x+iy-y)/rate;
                if (crossing>t && crossing<end) end=crossing;
            }
        }
        float a=z+(tz-z)*t, b=z+(tz-z)*end;
        float fb=fa,cb=ca;
        if (slope) {
            float da=surface_delta(ix,iy,x+t*dx-ix,y+t*dy-iy,c->kind==3);
            float db=surface_delta(ix,iy,x+end*dx-ix,y+end*dy-iy,c->kind==3);
            if (c->kind==2) { fa-=da; fb-=db; }
            else { ca+=da; cb+=db; }
        }
        if (solid || a<=fa || b<=fb || a>=ca || b>=cb) {
            if (hit) {
                if (solid && !wall) return 0;
                int axis=entry_axis;
                if (!solid && a>fa && a<ca) {
                    if (b>=cb && !roof) return 0; // open sky
                    float before=b<=fb ? a-fa : ca-a;
                    float after=b<=fb ? b-fb : cb-b;
                    t+=(end-t)*before/(before-after);
                    axis=2;
                }
                hit->x=x+t*dx; hit->y=y+t*dy; hit->z=z+t*(tz-z); hit->axis=axis;
                if (axis==0) hit->x-=sx*.015f;
                if (axis==1) hit->y-=sy*.015f;
                if (axis==2) hit->z+=tz<z?.015f:-.015f;
                if (axis>=3 && k) {
                    float bias=k->nx*dx+k->ny*dy>=0?-.011f:.011f;
                    hit->x+=bias*k->nx; hit->y+=bias*k->ny;
                }
            }
            return 1;
        }
        if (end >= 1) return 0;
        t = end;
        if (end<boundary) {
            if (k) entry_axis=k->nx==k->ny?3:4;
            continue;
        }
        if (nx <= end) { ix += sx; nx += step_x; entry_axis=0; }
        if (ny <= end) { iy += sy; ny += step_y; entry_axis=1; }
    }
}
int visible(float x,float y,float z,float tx,float ty,float tz) {
    return !trace_surface(x,y,z,tx,ty,tz,NULL);
}

// A reverse breadth-first search gives the next walkable cell from any spawn.
// Directed edges allow drops while enforcing the same step height as movement.
static uint8_t next_step[MAP*MAP];
static uint16_t queue[MAP*MAP];
static const int vx[] = {1,-1,0,0}, vy[] = {0,0,1,-1};
static int goal_x = -1, goal_y = -1;
// The map is immutable. Bake directed arrival edges once so a route refresh
// during combat only visits bytes and queue entries.
static uint8_t arrivals[MAP*MAP];
void navigation_prepare(void) {
    for (int y=0; y<MAP; ++y) for (int x=0; x<MAP; ++x) {
        const Cube *dest=map_cube(x,y);
        unsigned mask=0;
        if (!map_solid(dest) && bot_clip_clear(x+.5f,y+.5f,map_floor(dest))) for (int i=0; i<4; ++i) {
            const Cube *from=map_cube(x-vx[i],y-vy[i]);
            if (fits(from,map_floor(from)) && fits(dest,map_floor(from))) mask |= 1u<<i;
        }
        arrivals[y*MAP+x]=(uint8_t)mask;
    }
    goal_x=goal_y=-1;
}
void route_to(int x, int y) {
    // A player can stand in the open tip of a corner whose center cannot
    // hold the bot. End the grid route on an adjoining open face.
    if (map_cube(x,y)->kind==1) {
        const MapCorner *k=corner_at(x,y);
        if (arrivals[y*MAP+x-k->nx]) x-=k->nx;
        else if (arrivals[(y-k->ny)*MAP+x]) y-=k->ny;
    }
    if (goal_x == x && goal_y == y) return;
    goal_x = x; goal_y = y;
    memset(next_step, 255, sizeof(next_step));
    int head = 0, tail = 0, id = y*MAP+x;
    queue[tail++] = (uint16_t)id;
    next_step[id] = 4;
    while (head < tail) {
        id = queue[head++];
        unsigned mask=arrivals[id];
        for (int i=0; i<4; ++i) {
            if (!(mask & (1u<<i))) continue;
            int previous=id-vx[i]-vy[i]*MAP;
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
