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

static void clip_space(float x,float y,float feet,float radius,float *floor,float *ceiling) {
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

#include "generated/ladders.h"
int ladder_space(float x,float y,float feet,float *top) {
    for (unsigned i=0;i<sizeof(ladders)/sizeof(ladders[0]);++i) {
        const short *p=ladders[i];
        float dx=x-p[0],dy=y-p[1],end=map_cube(p[0],p[1])->floor+p[2];
        if (dx*dx+dy*dy<2.25f && feet<=end) { *top=end; return 1; }
    }
    return 0;
}
static const MapCorner *body_bounds(float x,float y,float feet,float radius,unsigned clipmask,float *floor,float *ceiling) {
    const MapCorner *surface=NULL;
    *floor=-128; *ceiling=127;
    float left=x-radius,right=x+radius,top=y-radius,bottom=y+radius;
    for (int iy=(int)top;iy<=(int)bottom;++iy) for (int ix=(int)left;ix<=(int)right;++ix) {
        const Cube *c=map_cube(ix,iy);
        if (c->kind==1) {
            const MapCorner *k=corner_at(ix,iy);
            float u=k->nx>0?right:left,v=k->ny>0?bottom:top;
            int match=ix==(int)u && iy==(int)v;
            int inner=k->nx*u+k->ny*v>=k->plane;
            if (match && inner) surface=k;
            if ((match && inner && k->solid) || ((k->solid || (match && inner)) && (c->tag&clipmask))) {
                *floor=127; *ceiling=-128; return match && inner?k:NULL;
            }
            if (k->solid) inner=0;
            *floor=fmaxf(*floor,inner?k->inner_floor:k->outer_floor);
            *ceiling=fminf(*ceiling,inner?k->inner_ceil:k->outer_ceil);
            continue;
        }
        if (map_solid(c) || (c->tag&clipmask)) { *floor=127; *ceiling=-128; return NULL; }
        *floor=fmaxf(*floor,map_floor(c)); *ceiling=fminf(*ceiling,map_ceiling(c));
    }
    clip_space(x,y,feet,radius,floor,ceiling);
    return surface;
}
int grenade_space(float x,float y,float feet,float *floor,float *ceiling) {
    const MapCorner *corner=body_bounds(x,y,feet,.2f,64,floor,ceiling);
    return corner?(corner->nx==corner->ny?-1:1):0;
}
void body_space(float x,float y,float feet,float *floor,float *ceiling) {
    body_bounds(x,y,feet,PLAYER_RADIUS,192,floor,ceiling);
}
static int body_fits(float x,float y,float feet,float height,float step) {
    float floor,ceiling;
    body_space(x,y,feet,&floor,&ceiling);
    return floor<=feet+step && fmaxf(floor,feet)+height<=ceiling;
}
static int near_actor(const ActorBounds *a,float x,float y) {
    return a && fabsf(a->x-x)<2*PLAYER_RADIUS && fabsf(a->y-y)<2*PLAYER_RADIUS;
}
void actor_clearance(const ActorBounds *a,float x,float y,float feet,float height,float *floor,float *ceiling) {
    if (!near_actor(a,x,y)) return;
    if (feet>=a->top-.01f) *floor=fmaxf(*floor,a->top);
    else if (feet+height<=a->feet+.01f) *ceiling=fminf(*ceiling,a->feet);
}
static int actor_blocks(const ActorBounds *a,float x,float y,float dx,float dy,float feet,float height) {
    // PC plcollide uses square XY bounds when either fighter is a bot.
    return near_actor(a,x,y) && feet+height>a->feet && feet<a->top-.01f &&
           (a->x-x)*dx+(a->y-y)*dy>=0;
}
void walk_past_actor(float *x,float *y,float dx,float dy,float feet,float height,float step,const ActorBounds *a) {
    int steps=1+(int)(fmaxf(fabsf(dx),fabsf(dy))*3);
    for (int i=0;i<steps;++i) {
        float sx=dx/steps,sy=dy/steps,nx=*x+sx,ny=*y+sy,floor,ceiling;
        const MapCorner *corner=body_bounds(nx,ny,feet,PLAYER_RADIUS,192,&floor,&ceiling);
        if (!actor_blocks(a,nx,ny,sx,sy,feet,height) && floor<=feet+step && fmaxf(floor,feet)+height<=ceiling) {
            *x=nx; *y=ny; continue;
        }
        if (corner) {
            // Project the displacement onto the diagonal and use the PC's
            // 1/42 backoff so the next update starts clear of the wall.
            float normal=(sx*corner->nx+sy*corner->ny)*.5f;
            float slide_x=nx-normal*corner->nx-sx/42;
            float slide_y=ny-normal*corner->ny-sy/42;
            if (!actor_blocks(a,slide_x,slide_y,slide_x-*x,slide_y-*y,feet,height) && body_fits(slide_x,slide_y,feet,height,step)) {
                *x=slide_x; *y=slide_y; continue;
            }
        }
        if (!actor_blocks(a,nx,*y,sx,0,feet,height) && body_fits(nx,*y,feet,height,step)) *x=nx;
        if (!actor_blocks(a,*x,ny,0,sy,feet,height) && body_fits(*x,ny,feet,height,step)) *y=ny;
    }
}
void walk_body(float *x,float *y,float dx,float dy,float feet,float height,float step) {
    walk_past_actor(x,y,dx,dy,feet,height,step,NULL);
}
float body_floor(float x,float y) {
    float floor,ceiling;
    body_space(x,y,map_floor(map_cube((int)x,(int)y)),&floor,&ceiling);
    return floor;
}
void walk(float *x,float *y,float dx,float dy) {
    walk_body(x,y,dx,dy,body_floor(*x,*y),PLAYER_EYE_HEIGHT+PLAYER_ABOVE_EYE,1.5f);
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
    if (hit) hit->fraction=1;
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
                int axis=entry_axis;
                int sky=solid && !wall;
                if (!solid && a>fa && a<ca) {
                    sky=b>=cb && !roof;
                    float before=b<=fb ? a-fa : ca-a;
                    float after=b<=fb ? b-fb : cb-b;
                    t+=(end-t)*before/(before-after);
                    axis=2;
                }
                hit->fraction=t; // Geometric depth before the bullet-mark offset, including sky.
                if (sky) return 0;
                hit->x=x+t*dx; hit->y=y+t*dy; hit->z=z+t*(tz-z); hit->axis=axis;
                // PC raycube supplies no decal normal for heightfield floors and ceilings.
                hit->can_mark=axis!=2 || !slope;
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
static int goal_x = -1, goal_y = -1, pickup_goal;
static float goal_height;
// The map is immutable. Bake directed arrival edges once so a route refresh
// during combat only visits bytes and queue entries.
static uint8_t arrivals[MAP*MAP];
void navigation_prepare(void) {
    const float height=PLAYER_EYE_HEIGHT+PLAYER_ABOVE_EYE;
    memset(arrivals,0,sizeof(arrivals));
    for (int y=1;y<MAP-1;++y) for (int x=1;x<MAP-1;++x) {
        if (map_cube(x,y)->kind==0) continue;
        int id=y*MAP+x;
        float cx=x+.5f,cy=y+.5f,floor,ceiling;
        body_space(cx,cy,map_floor(map_cube(x,y)),&floor,&ceiling);
        if (ceiling-floor<height) continue;
        for (int axis=0;axis<2;++axis) {
            int dx=axis==0,dy=axis==1;
            // A narrow strip between a step and a rotated crate can be lower
            // than either center. Check the rise and clearance on both sides.
            float previous=floor,previous_roof=ceiling;
            int forward=1,reverse=1;
            for (int sample=1;sample<=16 && (forward || reverse);++sample) {
                float sx=cx+dx*sample/16.f,sy=cy+dy*sample/16.f;
                float next,roof;
                body_space(sx,sy,map_floor(map_cube((int)sx,(int)sy)),&next,&roof);
                float clearance=fminf(roof,previous_roof)-fmaxf(previous,next);
                forward &= clearance>=height && next<=previous+1.5f;
                reverse &= clearance>=height && previous<=next+1.5f;
                previous=next; previous_roof=roof;
            }
            int neighbor=id+dx+dy*MAP;
            if (forward) arrivals[neighbor]|=1u<<(axis*2);
            if (reverse) arrivals[id]|=2u<<(axis*2);
        }
    }
    goal_x=goal_y=-1;
}
static void spread_route(int tail) {
    int head=0;
    while (head < tail) {
        int id = queue[head++];
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
void route_to(int x, int y) {
    // Near cover, a player's off-center position can fit even if the grid center cannot.
    if (!arrivals[y*MAP+x]) for (int i=0;i<4;++i) {
        int nx=x+vx[i],ny=y+vy[i];
        if ((unsigned)nx<MAP && (unsigned)ny<MAP && arrivals[ny*MAP+nx]) {
            x=nx; y=ny; break;
        }
    }
    if (!pickup_goal && goal_x == x && goal_y == y) return;
    pickup_goal=0;
    goal_x = x; goal_y = y;
    memset(next_step, 255, sizeof(next_step));
    int tail = 0, id = y*MAP+x;
    queue[tail++] = (uint16_t)id;
    next_step[id] = 4;
    spread_route(tail);
    // A target on a drop-only ledge can still be engaged from below.
    // Preserve paths onto the ledge and seed visible firing positions only
    // in parts of the graph that could not reach it on foot.
    tail=0;
    float eye=body_floor(x+.5f,y+.5f)+PLAYER_EYE_HEIGHT;
    for (int dy=-5;dy<=5;++dy) for (int dx=-5;dx<=5;++dx) {
        int nx=x+dx,ny=y+dy;
        if ((unsigned)nx>=MAP || (unsigned)ny>=MAP || dx*dx+dy*dy>25) continue;
        int candidate=ny*MAP+nx;
        if (next_step[candidate]!=255 || !arrivals[candidate]) continue;
        float z=body_floor(nx+.5f,ny+.5f)+PLAYER_EYE_HEIGHT;
        if (!visible(nx+.5f,ny+.5f,z,x+.5f,y+.5f,eye-1)) continue;
        queue[tail++]=(uint16_t)candidate;
        next_step[candidate]=4;
    }
    spread_route(tail);
}
void route_to_pickup(int x,int y,float z) {
    if (pickup_goal && goal_x==x && goal_y==y && goal_height==z) return;
    pickup_goal=1; goal_x=x; goal_y=y; goal_height=z;
    memset(next_step,255,sizeof(next_step));
    int tail=0;
    // Seed only cells where the bot can physically collect this item.
    for (int iy=y-2;iy<=y+2;++iy) for (int ix=x-2;ix<=x+2;++ix) {
        if ((unsigned)ix>=MAP || (unsigned)iy>=MAP) continue;
        int id=iy*MAP+ix;
        if (!arrivals[id]) continue;
        float dx=ix+.5f-x,dy=iy+.5f-y,dz=body_floor(ix+.5f,iy+.5f)-z;
        if (dx*dx+dy*dy+dz*dz>=6.25f) continue;
        next_step[id]=4;
        queue[tail++]=(uint16_t)id;
    }
    spread_route(tail);
}
int route_next(float x, float y, float *tx, float *ty) {
    int cx = (int)x, cy = (int)y, dir = next_step[cy*MAP+cx];
    if (dir == 255) return 0;
    *tx = cx+.5f; *ty = cy+.5f;
    if (dir < 4) {
        // Align with the corridor before turning around an inside corner.
        if (vx[dir] && fabsf(y-*ty) > .001f) *tx = x;
        else if (vy[dir] && fabsf(x-*tx) > .001f) *ty = y;
        else { *tx += vx[dir]; *ty += vy[dir]; }
    }
    return 1;
}
