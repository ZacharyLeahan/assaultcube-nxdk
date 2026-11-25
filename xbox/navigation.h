#ifndef AC_XBOX_NAVIGATION_H
#define AC_XBOX_NAVIGATION_H
#ifdef __cplusplus
extern "C" {
#endif
float body_floor(float x,float y);
void walk(float *x, float *y, float dx, float dy);
int grenade_space(float x,float y,float feet,float *floor,float *ceiling);
void body_space(float x,float y,float feet,float *floor,float *ceiling);
int ladder_space(float x,float y,float feet,float *top);
typedef struct {float x,y,feet,top;} ActorBounds;
void walk_past_actor(float *x,float *y,float dx,float dy,float feet,float height,float step,const ActorBounds *actor);
void actor_clearance(const ActorBounds *actor,float x,float y,float feet,float height,float *floor,float *ceiling);
void walk_body(float *x,float *y,float dx,float dy,float feet,float height,float step);
int visible(float x, float y, float z, float tx, float ty, float tz);
typedef struct { float x,y,z; int axis, can_mark; float fraction; } SurfaceHit;
int trace_surface(float x,float y,float z,float tx,float ty,float tz,SurfaceHit *hit);
void navigation_prepare(void);
void route_to(int x, int y);
void route_to_pickup(int x,int y,float z);
int route_next(float x, float y, float *tx, float *ty);
#ifdef __cplusplus
}
#endif
#endif
