#ifndef ASSAULTCUBE_RENDER_H
#define ASSAULTCUBE_RENDER_H

#include "range.h"

enum { W = 320, H = 240 };
typedef uint32_t RangeFrame[H][W];
// Returns whether the center pixel covers the visible target.
int render_scene(const Range *range, RangeFrame frame);
void render_overlay(const Range *range, int aimed, RangeFrame frame);

#endif
