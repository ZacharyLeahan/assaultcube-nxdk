#include "grenade.h"
#include "generated/water.h"
#include <math.h>

namespace {
bool fits(GrenadeFlight &g,MatchPoint &p,const ActorBounds *actors,int count,int &corner) {
    float floor,ceiling;
    corner=grenade_space(p.x,p.y,p.z-.3f,&floor,&ceiling);
    for(int i=0;i<count;++i) {
        const ActorBounds &a=actors[i];
        float dx=a.x-p.x,dy=a.y-p.y;
        if(fabsf(dx)>=PLAYER_RADIUS+.2f || fabsf(dy)>=PLAYER_RADIUS+.2f || dx*g.velocity.x+dy*g.velocity.y<0) continue;
        if(p.z-.3f<a.feet) ceiling=fminf(ceiling,a.feet-1);
        else floor=fmaxf(floor,a.top);
    }
    float gap=p.z-.3f-floor;
    if(ceiling-floor<.3f || gap<=-.01f || p.z-ceiling>.1f) return false;
    if(gap<0) p.z=floor+.3f;
    if(p.z>ceiling) {if(gap>.1f) p.z=ceiling;g.velocity.z=0;}
    g.on_floor=p.z-.3f-floor<.01f;
    return true;
}
void damp(MatchPoint &v,float factor) {v.x*=factor;v.y*=factor;v.z*=factor;}
}

bool grenade_step(GrenadeFlight &g,const ActorBounds *actors,int count,unsigned millis,int resolution) {
    MatchPoint &p=g.position,&v=g.velocity;
    bool water=p.z<WATER_LEVEL;
    if(g.on_floor) damp(v,1-millis/((water?20.f:6.f)*20));
    else v.z-=.5f*9.81f/4*(millis*.001f);
    float speed=millis*.03f/(water?12:1);
    MatchPoint d={v.x*speed/resolution,v.y*speed/resolution,v.z*speed/resolution};
    float length=sqrtf(d.x*d.x+d.y*d.y+d.z*d.z)*resolution;
    g.rotation+=6*length;
    bool bounced=false;
    for(int i=0;i<resolution;++i) {
        MatchPoint next={p.x+d.x,p.y+d.y,p.z+d.z};
        int corner=0;
        if(fits(g,next,actors,count,corner)) {p=next;continue;}
        bounced=true;
        if(corner) {
            int unused;
            next={p.x+corner*d.y,p.y+corner*d.x,p.z+d.z};
            if(fits(g,next,actors,count,unused)) {
                float x=d.x;d.x=corner*d.y;d.y=corner*x;
                x=v.x;v.x=corner*v.y;v.y=corner*x;damp(v,.7f);p=next;continue;
            }
        }
        next={p.x,p.y+d.y,p.z+d.z};
        if(fits(g,next,actors,count,corner)) {d.x=0;v.x=-v.x;damp(v,.7f);p=next;continue;}
        next={p.x+d.x,p.y,p.z+d.z};
        if(fits(g,next,actors,count,corner)) {d.y=0;v.y=-v.y;damp(v,.7f);p=next;continue;}
        next={p.x,p.y,p.z+d.z};
        if(fits(g,next,actors,count,corner)) {d.x=d.y=0;p=next;continue;}
        v.z=-v.z;damp(v,.5f);break;
    }
    bool sound=bounced && g.travel_since_bounce>=1.5f;
    g.travel_since_bounce=bounced?0:g.travel_since_bounce+length;
    return sound;
}
