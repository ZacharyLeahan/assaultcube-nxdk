#ifndef AC_XBOX_SHOT_LIGHT_H
#define AC_XBOX_SHOT_LIGHT_H
#include "match.h"

enum { BLAST_REACH=16, LIGHT_WIDTH_MAX=2*BLAST_REACH+1, MAX_DIRTY_LIGHTS=20 };
typedef struct { int x,y,width; } LightPatch;
void shot_light_init(void);
unsigned shot_light_update(const MatchView *view,LightPatch dirty[MAX_DIRTY_LIGHTS]);
const uint8_t *lit_cell(int x,int y);
#endif
