#include "bot_route.h"
#include "navigation.h"
#include <math.h>

namespace {
struct Waypoint { float x,y,z; unsigned short first; unsigned char count,flags; };
#include "generated/waypoints.h"
constexpr int count=sizeof(waypoints)/sizeof(waypoints[0]);
float distance2(MatchPoint from,const Waypoint &w) {
    float dx=w.x-from.x,dy=w.y-from.y,dz=w.z-from.z;
    return dx*dx+dy*dy+dz*dz;
}
int nearest(MatchPoint from,bool flood,float radius) {
    int best=-1;
    float least=radius*radius;
    for(int i=0;i<count;++i) {
        const Waypoint &w=waypoints[i];
        if(bool(w.flags&8)!=flood) continue;
        float d=distance2(from,w);
        if(d<least && visible(from.x,from.y,from.z,w.x,w.y,w.z)) {best=i;least=d;}
    }
    return best;
}
int patrol_next(MatchPoint from,int current,const int previous[5],int choice) {
    // Windows FindWaypoint takes the nearest unvisited neighbour, occasionally
    // choosing one of the nearest three. Avoid the last five waypoints.
    int candidates[3]={-1,-1,-1};
    float scores[3]={1e9f,1e9f,1e9f};
    const Waypoint &w=waypoints[current];
    for(int j=0;j<w.count;++j) {
        int n=links[w.first+j];
        bool visited=n==current;
        for(int k=0;k<5;++k) if(n==previous[k]) visited=true;
        if(visited) continue;
        float d=distance2(from,waypoints[n]);
        for(int k=0;k<3;++k) if(d<scores[k]) {
            for(int l=2;l>k;--l) {scores[l]=scores[l-1];candidates[l]=candidates[l-1];}
            scores[k]=d;candidates[k]=n;break;
        }
    }
    int available=(candidates[0]>=0)+(candidates[1]>=0)+(candidates[2]>=0);
    return available?candidates[choice%available]:-1;
}
}

void BotRoute::reset() {
    waypoint=-1;item=-2;fallback=jump=false;progress_at=0;closest=1e9f;
    for(int &n:previous) n=-1;
}
bool BotRoute::target(MatchPoint from,int pickup,unsigned now,unsigned (*random)(),MatchPoint &destination) {
    jump=false;
    if(item!=pickup) {
        reset();item=pickup;
        waypoint=nearest(from,false,pickup<0?10.f:15.f);
        if(waypoint<0) waypoint=nearest(from,true,5);
        progress_at=now;fallback=waypoint<0;
    }
    if(fallback) return false;
    const Waypoint &w=waypoints[waypoint];
    float dx=w.x-from.x,dy=w.y-from.y,d=sqrtf(dx*dx+dy*dy);
    if(d<((w.flags&16)?1.5f:2.5f) && fabsf(w.z-from.z)<6) {
        jump=(w.flags&16)!=0;
        int next=-1;
        if(pickup>=0) {
            int edge=pickup_links[pickup][waypoint];
            if(edge<w.count) next=links[w.first+edge];
        } else {
            int choice=0;
            if(now>=random_after && random()%100<20) {
                choice=random();random_after=now+10000;
            }
            next=patrol_next(from,waypoint,previous,choice);
            if(next<0 && previous[0]>=0) {
                for(int &n:previous) n=-1;
                next=patrol_next(from,waypoint,previous,0);
            }
        }
        for(int k=4;k>0;--k) previous[k]=previous[k-1];
        previous[0]=waypoint;
        if(next<0) {fallback=true;return false;}
        waypoint=next;closest=1e9f;progress_at=now;
    } else if(d+.15f<closest) {closest=d;progress_at=now;}
    else if(now-progress_at>900) {fallback=true;return false;}
    const Waypoint &goal=waypoints[waypoint];
    destination={goal.x,goal.y,goal.z};
    return true;
}
