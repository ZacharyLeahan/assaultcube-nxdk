#include <SDL.h>
#include <hal/debug.h>
#include <hal/video.h>
#include <hal/xbox.h>
#include <pbkit/pbkit.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "render.h"

extern const uint8_t shot_audio[] __asm__("_auto"), shot_end[] __asm__("_auto_end");
extern const uint8_t auto_reload[], auto_reload_end[];
static RangeFrame frame;
static SDL_AudioDeviceID audio_device;

static void sound(const uint8_t *begin, const uint8_t *end) {
    if (!audio_device) return;
    SDL_ClearQueuedAudio(audio_device);
    SDL_QueueAudio(audio_device, begin, (Uint32)((uintptr_t)end-(uintptr_t)begin));
}

static float axis(SDL_GameController *pad, SDL_GameControllerAxis a) {
    float v = SDL_GameControllerGetAxis(pad,a)/32768.f;
    return fabsf(v) < .18f ? 0 : copysignf((fabsf(v)-.18f)/.82f,v);
}

int main(void) {
    XVideoSetMode(640,480,32,REFRESH_DEFAULT);
    if (SDL_Init(SDL_INIT_GAMECONTROLLER) || pb_init()) { debugPrint("AssaultCube init failed"); return 1; }
    pb_show_front_screen();
    if (SDL_InitSubSystem(SDL_INIT_AUDIO)==0) {
        SDL_AudioSpec spec = {0};
        spec.freq = 48000; spec.format = AUDIO_S16LSB; spec.channels = 1; spec.samples = 1024;
        audio_device = SDL_OpenAudioDevice(NULL,0,&spec,NULL,0);
        if (audio_device) SDL_PauseAudioDevice(audio_device,0);
    }
    SDL_GameController *pads[4] = {0};
    unsigned last = SDL_GetTicks(), reset_time = 0;
    int previous_start = 0;
    Range range = {0};
    range_restart(&range);
    for (;;) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_CONTROLLERDEVICEADDED) {
                for (int i = 0; i < 4; ++i) if (!pads[i]) {
                    pads[i] = SDL_GameControllerOpen(event.cdevice.which);
                    break;
                }
            }
            if (event.type == SDL_CONTROLLERDEVICEREMOVED)
                for (int i = 0; i < 4; ++i) if (pads[i] && !SDL_GameControllerGetAttached(pads[i])) {
                    SDL_GameControllerClose(pads[i]); pads[i] = NULL;
                }
        }
        unsigned now = SDL_GetTicks(), elapsed = now-last;
        last = now;
        float move = 0, strafe = 0, turn = 0, look = 0;
        int fire = 0, refill = 0, start = 0, reset = 0, connected = 0;
        for (int i = 0; i < 4; ++i) if (pads[i]) {
            SDL_GameController *p = pads[i];
            connected = 1;
            move -= axis(p,SDL_CONTROLLER_AXIS_LEFTY); strafe += axis(p,SDL_CONTROLLER_AXIS_LEFTX);
            turn += axis(p,SDL_CONTROLLER_AXIS_RIGHTX); look -= axis(p,SDL_CONTROLLER_AXIS_RIGHTY);
            int rt = axis(p,SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > .25f;
            fire |= rt; refill |= SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_X);
            start |= SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_START);
            reset |= rt && axis(p,SDL_CONTROLLER_AXIS_TRIGGERLEFT) > .5f &&
                     SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_BACK) &&
                     SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
        }
        reset_time = reset ? reset_time+elapsed : 0;
        if (reset_time >= 1000) XReboot();
        RangeInput input = {
            .move = move, .strafe = strafe, .turn = turn, .look = look,
            .fire = fire, .refill = refill, .start = start && !previous_start
        };
        previous_start = start;
        range_move(&range, &input, elapsed);
        int aimed = render_scene(&range, frame);
        RangeSound effect = range_fire(&range, &input, aimed);
        if (effect == RANGE_SHOT) sound(shot_audio, shot_end);
        if (effect == RANGE_RELOAD) sound(auto_reload, auto_reload_end);
        render_overlay(&range, aimed, frame);
        pb_target_back_buffer();
        pb_reset();
        unsigned pitch_bytes = pb_back_buffer_pitch();
        uint8_t *back = (uint8_t *)pb_back_buffer();
        uint32_t row[W*2];
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) row[2*x] = row[2*x+1] = frame[y][x];
            memcpy(back+y*2*pitch_bytes,row,sizeof(row));
            memcpy(back+(y*2+1)*pitch_bytes,row,sizeof(row));
        }
        pb_erase_text_screen();
        pb_print("\n   ASSAULTCUBE  /  ac_desert\n   Range %d   MTP-57  %d/20  %s\n",range.hits,range.ammo,range.reload ? "RELOADING" : "");
        if (!connected) pb_print("   Connect an Xbox controller\n");
        pb_draw_text_screen();
        while (pb_busy()) {}
        while (pb_finished()) {}
    }
}
