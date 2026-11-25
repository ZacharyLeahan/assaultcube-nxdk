#include "controllers.h"

void refresh_pads(Pad pads[PAD_COUNT]) {
    // Device indices can change during a swap. Instance IDs follow the attachment.
    SDL_JoystickID ids[PAD_COUNT];
    int count = SDL_NumJoysticks();
    if (count < 0) return;
    if (count > PAD_COUNT) count = PAD_COUNT;
    for (int i = 0; i < count; ++i) ids[i] = SDL_JoystickGetDeviceInstanceID(i);

    for (int slot = 0; slot < PAD_COUNT; ++slot) {
        Pad *pad = &pads[slot];
        if (!pad->handle) continue;
        int i = 0;
        while (i < count && ids[i] != pad->id) ++i;
        if (i == count || !SDL_GameControllerGetAttached(pad->handle)) {
            SDL_GameControllerClose(pad->handle);
            *pad = (Pad){0};
        }
    }
    for (int i = 0; i < count; ++i) {
        if (ids[i] < 0 || SDL_GameControllerFromInstanceID(ids[i])) continue;
        for (int slot = 0; slot < PAD_COUNT; ++slot) {
            if (pads[slot].handle) continue;
            if (SDL_IsGameController(i)) {
                pads[slot].handle = SDL_GameControllerOpen(i);
                pads[slot].id = ids[i];
            }
            break;
        }
    }
}
