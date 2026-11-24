#ifndef ASSAULTCUBE_RENDER_H
#define ASSAULTCUBE_RENDER_H

#include "match.h"

enum { W = 320, H = 240 };
typedef uint32_t MatchFrame[H][W];
// Returns whether the center pixel covers the visible target.
int render_scene(const MatchView *view, MatchFrame frame);
void render_overlay(const MatchView *view, int aimed, MatchFrame frame);

#endif
