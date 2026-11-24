#ifndef AC_GPU_RENDER_H
#define AC_GPU_RENDER_H
#include "match.h"
int gpu_init(int width, int height);
void gpu_present(const MatchView *view, int aimed, unsigned fps10, int controllers);
#endif
