#ifndef AC_XBOX_NAVIGATION_H
#define AC_XBOX_NAVIGATION_H
#ifdef __cplusplus
extern "C" {
#endif
void walk(float *x, float *y, float dx, float dy);
void body_space(float x,float y,float *floor,float *ceiling);
int ladder_space(float x,float y,float feet,float *top);
void walk_body(float *x,float *y,float dx,float dy,float feet,float height,float step);
int visible(float x, float y, float z, float tx, float ty, float tz);
typedef struct { float x,y,z; int axis; } SurfaceHit;
int trace_surface(float x,float y,float z,float tx,float ty,float tz,SurfaceHit *hit);
void navigation_prepare(void);
void route_to(int x, int y);
int route_next(float x, float y, float *tx, float *ty);
#ifdef __cplusplus
}
#endif
#endif
