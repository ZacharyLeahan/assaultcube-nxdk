#ifndef AC_XBOX_GEOMETRY_H
#define AC_XBOX_GEOMETRY_H
#include "match.h"
typedef struct { float x,y,z,u,v; unsigned char rgba[4]; } Vertex;
#ifdef __cplusplus
extern "C" {
#endif
extern const MatchPoint prop_triangles[];
float prop_depth(MatchPoint origin,MatchPoint direction,float depth);
#ifdef __cplusplus
}
#endif
#endif
