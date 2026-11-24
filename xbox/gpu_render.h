#ifndef AC_GPU_RENDER_H
#define AC_GPU_RENDER_H
#include "match.h"
void gpu_begin_input(unsigned refresh_hz, int respawning);
void gpu_trace_init(void);
int gpu_init(int width, int height);
unsigned gpu_present(const MatchView *view, int aimed, unsigned fps10, int controllers);
#endif
