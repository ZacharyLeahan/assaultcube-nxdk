#include "geometry.h"
#include <math.h>

typedef struct { float low[3],high[3]; unsigned first,count; } PropMesh;
#include "generated/prop_bounds.h"

static MatchPoint cross(MatchPoint a,MatchPoint b) {
    return (MatchPoint){a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}
static float dot(MatchPoint a,MatchPoint b) { return a.x*b.x+a.y*b.y+a.z*b.z; }

float prop_depth(MatchPoint origin,MatchPoint direction,float depth) {
    const float o[]={origin.x,origin.y,origin.z},d[]={direction.x,direction.y,direction.z};
    float inverse[3];
    for (int axis=0;axis<3;++axis) inverse[axis]=d[axis]!=0?1/d[axis]:0;
    for (unsigned i=0;i<sizeof(props)/sizeof(props[0]);++i) {
        const PropMesh *mesh=props+i;
        float near=0,far=depth;
        for (int axis=0;axis<3;++axis) {
            if (!inverse[axis]) {
                if (o[axis]<mesh->low[axis] || o[axis]>mesh->high[axis]) { far=-1; break; }
                continue;
            }
            float a=(mesh->low[axis]-o[axis])*inverse[axis];
            float b=(mesh->high[axis]-o[axis])*inverse[axis];
            if (inverse[axis]<0) { float swap=a; a=b; b=swap; }
            if (a>near) near=a;
            if (b<far) far=b;
            if (near>far) break;
        }
        if (near>far) continue;
        for (unsigned j=0;j<mesh->count;j+=3) {
            const Vertex *v=gpu_vertices+mesh->first+j;
            MatchPoint ab={v[1].x-v->x,v[1].y-v->y,v[1].z-v->z};
            MatchPoint ac={v[2].x-v->x,v[2].y-v->y,v[2].z-v->z};
            MatchPoint normal=cross(direction,ac);
            float det=dot(ab,normal);
            if (fabsf(det)<.000001f) continue;
            MatchPoint offset={origin.x-v->x,origin.y-v->y,origin.z-v->z};
            float u=dot(offset,normal)/det;
            if (u<0 || u>1) continue;
            MatchPoint q=cross(offset,ab);
            float b=dot(direction,q)/det;
            if (b<0 || u+b>1) continue;
            float t=dot(ac,q)/det;
            if (t>=0 && t<depth) depth=t;
        }
    }
    return depth;
}
