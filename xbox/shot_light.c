#include "shot_light.h"
#include <math.h>
#include <string.h>

extern const uint8_t cell_light[MAP][MAP][3];
static uint8_t grid[MAP][MAP][4];
enum { SOURCE_CAPACITY=2+2*8 };
typedef struct { int x,y,reach,scale; int rgb[3]; } LightSource;
static LightSource previous[SOURCE_CAPACITY];
static unsigned previous_count;

void shot_light_init(void) {
    for (int y=0;y<MAP;++y) for (int x=0;x<MAP;++x) {
        memcpy(grid[y][x],cell_light[y][x],3);
        grid[y][x][3]=255;
    }
    previous_count=0;
}
const uint8_t *lit_cell(int x,int y) { return grid[y][x]; }

// Reproduce the fixed-point coloured lightray in worldlight.cpp, with
// flicker disabled as in dodynlights. Solid cells receive light and stop it.
static void ray(LightSource l,float bx,float by) {
    float dx=bx-l.x,dy=by-l.y;
    int steps=(int)(l.reach*l.reach*1.6f/sqrtf(dx*dx+dy*dy));
    int x=l.x*4096,y=l.y*4096,sx=(int)(dx*4096/steps),sy=(int)(dy*4096/steps);
    int power[3],fall[3];
    for (int k=0;k<3;++k) {
        int full=l.rgb[k]*l.scale;
        power[k]=full/4; fall[k]=full/steps/4;
    }
    for (int i=0;i<steps;++i) {
        int cx=x>>12,cy=y>>12;
        uint8_t *pixel=grid[cy][cx];
        for (int k=0;k<3;++k) {
            int sum=pixel[k]+(power[k]>>12);
            pixel[k]=(uint8_t)(sum>255?255:sum);
            power[k]-=fall[k]; fall[k]-=25;
        }
        if (map_cube(cx,cy)->kind==0) break;
        x+=sx; y+=sy;
    }
}
static void illuminate(LightSource l) {
    for (float x=l.x-l.reach;x<=l.x+l.reach;x+=1.6f) {
        ray(l,x,l.y-l.reach); ray(l,x,l.y+l.reach);
    }
    for (float y=l.y-l.reach+.8f;y<=l.y+l.reach-.8f;y+=1.6f) {
        ray(l,l.x-l.reach,y); ray(l,l.x+l.reach,y);
    }
}
static void smooth(LightPatch p) {
    // Windows filters alternating rows and columns in place.
    for (int row=0;row<2;++row) for (int j=p.width-1-row;j>=0;j-=2)
        for (int col=0;col<2;++col) for (int i=col;i<p.width;i+=2) {
            int x=p.x+i,y=p.y+j;
            for (int k=0;k<3;++k) {
                int cross=grid[y][x][k]+grid[y-1][x][k]+grid[y+1][x][k]
                         +grid[y][x-1][k]+grid[y][x+1][k];
                int corners=grid[y-1][x-1][k]+grid[y-1][x+1][k]
                           +grid[y+1][x-1][k]+grid[y+1][x+1][k];
                grid[y][x][k]=(2*cross+corners)/14;
            }
        }
}
static LightPatch patch(LightSource l) {
    return (LightPatch){l.x-l.reach,l.y-l.reach,2*l.reach+1};
}
static int contains(LightPatch a,LightPatch b) {
    return b.x>=a.x && b.y>=a.y && b.x+b.width<=a.x+a.width && b.y+b.width<=a.y+a.width;
}
static void add_dirty(LightPatch *patches,unsigned *count,LightPatch p) {
    for (unsigned i=0;i<*count;++i) if (contains(patches[i],p)) return;
    patches[(*count)++]=p;
}
static void add_source(LightSource *sources,unsigned *n,LightSource l) {
    // The playable area on ac_desert has room for the entire filter footprint.
    if(l.scale<=0 || l.x<=l.reach || l.y<=l.reach ||
       l.x>=MAP-1-l.reach || l.y>=MAP-1-l.reach) return;
    sources[(*n)++]=l;
}
unsigned shot_light_update(const MatchView *v,LightPatch dirty[MAX_DIRTY_LIGHTS]) {
    LightSource current[SOURCE_CAPACITY];
    unsigned n=0,count=0;
    add_source(current,&n,(LightSource){(int)v->px,(int)v->py,4,(int)v->player_light*4096/50,{96,80,64}});
    add_source(current,&n,(LightSource){(int)v->tx,(int)v->ty,4,(int)v->bot_light*4096/50,{96,80,64}});
    for(unsigned i=0;i<v->explosion_count;++i) {
        const ExplosionView *e=&v->explosions[i];
        if(e->end_ms<=v->elapsed_ms) continue;
        int remaining=e->end_ms-v->elapsed_ms,x=(int)e->pos.x,y=(int)e->pos.y;
        int flash=remaining-400;
        if(flash>0) add_source(current,&n,(LightSource){x,y,BLAST_REACH,(flash<100?flash:100)*4096/100,{255,255,224}});
        add_source(current,&n,(LightSource){x,y,BLAST_REACH,remaining*4096/600,{192,160,128}});
    }
    if(n==previous_count && !memcmp(previous,current,n*sizeof(*current))) return 0;
    for(unsigned i=0;i<previous_count;++i) add_dirty(dirty,&count,patch(previous[i]));
    for(unsigned i=0;i<count;++i) {
        LightPatch p=dirty[i];
        for(int y=p.y;y<p.y+p.width;++y) for(int x=p.x;x<p.x+p.width;++x)
            memcpy(grid[y][x],cell_light[y][x],3);
    }
    LightPatch pending={0};
    for(unsigned i=0;i<n;++i) {
        illuminate(current[i]);
        LightPatch p=patch(current[i]);
        add_dirty(dirty,&count,p);
        if(pending.width && contains(pending,p)) continue;
        if(pending.width && !contains(p,pending)) smooth(pending);
        pending=p;
    }
    if(pending.width) smooth(pending);
    memcpy(previous,current,n*sizeof(*current));previous_count=n;
    return count;
}
