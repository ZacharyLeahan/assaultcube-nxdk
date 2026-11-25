#define SDL_MAIN_HANDLED
#include "../controllers.h"
#include <assert.h>
#include <stdio.h>

static void poll(Pad pads[PAD_COUNT]) {
    SDL_PumpEvents();
    SDL_FlushEvents(SDL_FIRSTEVENT,SDL_LASTEVENT);
    refresh_pads(pads);
}

static Pad *find(Pad pads[PAD_COUNT], SDL_JoystickID id) {
    for (int i = 0; i < PAD_COUNT; ++i)
        if (pads[i].handle && pads[i].id == id) return &pads[i];
    return NULL;
}

static int attach(void) {
    int index = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER,
                                         SDL_CONTROLLER_AXIS_MAX, SDL_CONTROLLER_BUTTON_MAX, 0);
    assert(index >= 0);
    return index;
}

int main(void) {
    SDL_SetMainReady();
    assert(SDL_Init(SDL_INIT_GAMECONTROLLER) == 0);
    if (SDL_NumJoysticks() > PAD_COUNT-2) {
        puts("Two free controller slots are required for the virtual swap test.");
        SDL_Quit();
        return 77;
    }
    Pad pads[PAD_COUNT] = {0};
    poll(pads);
    int first = attach();
    int second = attach();
    SDL_JoystickID removed = SDL_JoystickGetDeviceInstanceID(first);
    SDL_JoystickID other = SDL_JoystickGetDeviceInstanceID(second);
    poll(pads);
    Pad *old = find(pads,removed);
    assert(old && find(pads,other));
    SDL_GameController *retained = find(pads,other)->handle;
    old->previous_start = old->previous_swap = old->previous_reload = 1;
    old->reset_ms = 900;
    assert(SDL_JoystickDetachVirtual(first) == 0);
    // Simulate an entire swap between two game frames, with the other pad still attached.
    int replacement = attach();
    SDL_JoystickID fresh = SDL_JoystickGetDeviceInstanceID(replacement);
    poll(pads);
    assert(!find(pads,removed) && find(pads,other)->handle == retained);
    Pad *pad = find(pads,fresh);
    assert(pad && !pad->previous_start && !pad->previous_swap && !pad->previous_reload && !pad->reset_ms);
    SDL_Joystick *joystick = SDL_GameControllerGetJoystick(pad->handle);
    assert(SDL_JoystickSetVirtualButton(joystick, SDL_CONTROLLER_BUTTON_START, 1) == 0);
    poll(pads);
    assert(SDL_GameControllerGetButton(pad->handle,SDL_CONTROLLER_BUTTON_START));
    assert(!SDL_GameControllerGetButton(retained,SDL_CONTROLLER_BUTTON_START));
    assert(SDL_JoystickDetachVirtual(replacement) == 0);
    poll(pads);
    assert(!find(pads,fresh) && find(pads,other)->handle == retained);
    for (int i = 0; i < PAD_COUNT; ++i)
        if (pads[i].handle) SDL_GameControllerClose(pads[i].handle);
    SDL_Quit();
    puts("Passed late attach, device reindexing, swap, replacement input and removal.");
}
