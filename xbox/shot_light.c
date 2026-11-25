#include "shot_light.h"
#include <math.h>
#include <string.h>

extern const uint8_t cell_light[MAP][MAP][3];
static uint8_t grid[MAP][MAP][4];
static ShotLight previous[2];

void shot_light_init(void) {
    for (int y=0;y<MAP;++y) for (int x=0;x<MAP;++x) {
        memcpy(grid[y][x],cell_light[y][x],3);
        grid[y][x][3]=255;
    }
    memset(previous,0,sizeof(previous));
}
const uint8_t *lit_cell(int x,int y) { return grid[y][x]; }

// Reproduce the fixed-point coloured lightray in worldlight.cpp, with
// flicker disabled as in dodynlights. Solid cells receive light and stop it.
static void ray(ShotLight l,float bx,float by) {
    float dx=bx-l.x,dy=by-l.y;
    int steps=(int)(LIGHT_REACH*LIGHT_REACH*1.6f/sqrtf(dx*dx+dy*dy));
    int x=l.x*4096,y=l.y*4096,sx=(int)(dx*4096/steps),sy=(int)(dy*4096/steps);
    int power[3],fall[3];
    const int rgb[]={96,80,64};
    for (int k=0;k<3;++k) {
        int full=rgb[k]*(int)(l.strength*4096/50);
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
static void illuminate(ShotLight l) {
    for (float x=l.x-LIGHT_REACH;x<=l.x+LIGHT_REACH;x+=1.6f) {
        ray(l,x,l.y-LIGHT_REACH); ray(l,x,l.y+LIGHT_REACH);
    }
    for (float y=l.y-LIGHT_REACH+.8f;y<=l.y+LIGHT_REACH-.8f;y+=1.6f) {
        ray(l,l.x-LIGHT_REACH,y); ray(l,l.x+LIGHT_REACH,y);
    }
}
static void smooth(LightPatch p) {
    // Windows filters alternating rows and columns in place.
    for (int row=0;row<2;++row) for (int j=LIGHT_WIDTH-1-row;j>=0;j-=2)
        for (int col=0;col<2;++col) for (int i=col;i<LIGHT_WIDTH;i+=2) {
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
static LightPatch patch(ShotLight l) { return (LightPatch){l.x-LIGHT_REACH,l.y-LIGHT_REACH}; }
static void add_dirty(LightPatch patches[4],unsigned *count,LightPatch p) {
    for (unsigned i=0;i<*count;++i) if (patches[i].x==p.x && patches[i].y==p.y) return;
    patches[(*count)++]=p;
}
unsigned shot_light_update(const ShotLight lights[2],LightPatch dirty[4]) {
    ShotLight current[2]={lights[0],lights[1]};
    for (int i=0;i<2;++i) {
        ShotLight l=current[i];
        // ac_desert's traversable area lies inside this border.
        if (!l.strength || l.x<=LIGHT_REACH || l.y<=LIGHT_REACH ||
            l.x>=MAP-1-LIGHT_REACH || l.y>=MAP-1-LIGHT_REACH) current[i]=(ShotLight){0};
    }
    if (!memcmp(previous,current,sizeof(current))) return 0;
    unsigned count=0;
    for (int i=0;i<2;++i) if (previous[i].strength) {
        LightPatch p=patch(previous[i]); add_dirty(dirty,&count,p);
        for (int y=p.y;y<p.y+LIGHT_WIDTH;++y) for (int x=p.x;x<p.x+LIGHT_WIDTH;++x)
            memcpy(grid[y][x],cell_light[y][x],3);
    }
    for (int i=0;i<2;++i) if (current[i].strength) illuminate(current[i]);
    for (int i=0;i<2;++i) if (current[i].strength) {
        if (i && current[0].strength && current[0].x==current[i].x && current[0].y==current[i].y) continue;
        LightPatch p=patch(current[i]);
        smooth(p); add_dirty(dirty,&count,p);
    }
    memcpy(previous,current,sizeof(previous));
    return count;
}
