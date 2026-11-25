#ifndef AC_XBOX_SHOT_LIGHT_H
#define AC_XBOX_SHOT_LIGHT_H
#include "match.h"

enum { LIGHT_REACH=4, LIGHT_WIDTH=2*LIGHT_REACH+1 };
typedef struct { int x,y; unsigned strength; } ShotLight;
typedef struct { int x,y; } LightPatch;
void shot_light_init(void);
unsigned shot_light_update(const ShotLight lights[2],LightPatch dirty[4]);
const uint8_t *lit_cell(int x,int y);
#endif
