#ifndef ASSAULTCUBE_RANGE_H
#define ASSAULTCUBE_RANGE_H

#include <stdint.h>

enum { MAP = 256 };
typedef struct {
    uint8_t kind;
    int8_t floor, ceil;
    uint8_t wall, ground, roof, upper, tag;
} Cube;
typedef struct {
    float px, py, eye, angle, pitch, tx, ty;
    int ammo, hits, health, spawn;
    unsigned cooldown, reload, flash, hitflash;
} Range;
typedef struct {
    float move, strafe, turn, look;
    int fire, refill, start;
} RangeInput;
typedef enum { RANGE_SILENT, RANGE_SHOT, RANGE_RELOAD } RangeSound;

const Cube *range_cube_at(int x, int y);
int range_cube_is_solid(const Cube *cube);
void range_restart(Range *r);
void range_move(Range *r, const RangeInput *input, unsigned elapsed);
RangeSound range_fire(Range *r, const RangeInput *input, int aimed);

#endif
