#ifndef AC_XBOX_CONTROLLERS_H
#define AC_XBOX_CONTROLLERS_H

#include <SDL.h>

enum { PAD_COUNT = 4 };
typedef struct {
    SDL_GameController *handle;
    SDL_JoystickID id;
    int previous_start, previous_swap, previous_reload, previous_knife, previous_melee;
    unsigned reset_ms;
} Pad;

void refresh_pads(Pad pads[PAD_COUNT]);

#endif
