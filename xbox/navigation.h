#ifndef AC_XBOX_NAVIGATION_H
#define AC_XBOX_NAVIGATION_H
#ifdef __cplusplus
extern "C" {
#endif
void walk(float *x, float *y, float dx, float dy);
int visible(float x, float y, float z, float tx, float ty, float tz);
void route_to(int x, int y);
int route_next(float x, float y, float *tx, float *ty);
#ifdef __cplusplus
}
#endif
#endif
