#include <SDL.h>
#include <hal/debug.h>
#include <hal/video.h>
#include <hal/xbox.h>
#include <pbkit/pbkit.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "gpu_render.h"
#include "controllers.h"

extern const uint8_t shot_audio[] __asm__("_auto"), shot_end[] __asm__("_auto_end");
extern const uint8_t auto_reload[], auto_reload_end[];
extern const uint8_t usp[], usp_end[], pistol_reload[], pistol_reload_end[];
static SDL_AudioDeviceID audio_device;

typedef struct { const uint8_t *data; Uint32 left; } Voice;
static Voice voices[2];

static void mix_audio(void *unused, Uint8 *out, int len) {
    (void)unused;
    memset(out,0,len);
    for (int i = 0; i < 2; ++i) {
        Voice *voice = &voices[i];
        Uint32 n = voice->left < (Uint32)len ? voice->left : (Uint32)len;
        if (!n) continue;
        SDL_MixAudioFormat(out,voice->data,AUDIO_S16LSB,n,i ? 42 : 96);
        voice->data += n; voice->left -= n;
    }
}
static void sound(MatchSound effect, int channel) {
    if (!audio_device || effect == SILENT) return;
    const uint8_t *starts[] = {NULL, shot_audio, auto_reload, usp, pistol_reload};
    const uint8_t *ends[] = {NULL, shot_end, auto_reload_end, usp_end, pistol_reload_end};
    SDL_LockAudioDevice(audio_device);
    voices[channel].data = starts[effect];
    voices[channel].left = (Uint32)((uintptr_t)ends[effect]-(uintptr_t)starts[effect]);
    SDL_UnlockAudioDevice(audio_device);
}

static float axis(SDL_GameController *pad, SDL_GameControllerAxis a) {
    float v = SDL_GameControllerGetAxis(pad,a)/32768.f;
    return fabsf(v) < .18f ? 0 : copysignf((fabsf(v)-.18f)/.82f,v);
}

// A circular deadzone lets small diagonal aim corrections follow the stick direction.
static void read_aim(SDL_GameController *pad, float *turn, float *look) {
    float x=SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_RIGHTX)/32768.f;
    float y=SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_RIGHTY)/32768.f;
    float magnitude=sqrtf(x*x+y*y);
    const float deadzone=.10f;
    if (magnitude<=deadzone) return;
    float travel=(fminf(1,magnitude)-deadzone)/(1-deadzone);
    // Keep the onset responsive and give fine corrections more stick travel.
    float scale=travel*(.45f+.55f*travel)/magnitude;
    *turn+=x*scale;
    *look-=y*scale;
}

int main(void) {
    if (!XVideoSetMode(1280,720,32,REFRESH_60HZ)) XVideoSetMode(640,480,32,REFRESH_DEFAULT);
    VIDEO_MODE mode = XVideoGetMode();
    if (SDL_Init(SDL_INIT_GAMECONTROLLER) || !gpu_init(mode.width,mode.height)) { debugPrint("AssaultCube init failed"); return 1; }
    if (SDL_InitSubSystem(SDL_INIT_AUDIO)==0) {
        SDL_AudioSpec spec = {0};
        spec.freq = 48000; spec.format = AUDIO_S16LSB; spec.channels = 1; spec.samples = 1024; spec.callback = mix_audio;
        audio_device = SDL_OpenAudioDevice(NULL,0,&spec,NULL,0);
        if (audio_device) SDL_PauseAudioDevice(audio_device,0);
    }
    Pad pads[PAD_COUNT] = {0};
    unsigned last = SDL_GetTicks();
    unsigned sample_start = last, frames = 0, fps10 = 0;
    unsigned previous_vblank=pb_get_vbl_counter();
    unsigned refreshes=1;
    unsigned refresh_hz=mode.refresh==50 ? 50 : 60;
    float refresh_period=1.f/refresh_hz;
    MatchView view = {0};
    gpu_trace_init();
    match_prepare();
    for (;;) {
        gpu_begin_input(refresh_hz,view.respawning);
        SDL_PumpEvents();
        SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
        refresh_pads(pads);
        unsigned now = SDL_GetTicks(), elapsed = now-last;
        last = now;
        if (now-sample_start >= 2000) {
            fps10 = (frames*10000u + (now-sample_start)/2)/(now-sample_start);
            sample_start = now;
            frames = 0;
        }
        float move = 0, strafe = 0, turn = 0, look = 0;
        int classic=0, tuned=0;
        int jump=0, crouch=0;
        int swap = 0, fire = 0, refill = 0, start = 0, connected = 0;
        for (int i = 0; i < PAD_COUNT; ++i) if (pads[i].handle) {
            Pad *pad = &pads[i];
            SDL_GameController *p = pad->handle;
            ++connected;
            move -= axis(p,SDL_CONTROLLER_AXIS_LEFTY); strafe += axis(p,SDL_CONTROLLER_AXIS_LEFTX);
            read_aim(p,&turn,&look);
            jump |= SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_A);
            crouch |= SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_B);
            int rt = axis(p,SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > .25f;
            fire |= rt; refill |= SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_X);
            int y = SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_Y);
            int menu = SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_START);
            classic |= SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_DPAD_LEFT);
            tuned |= SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
            swap |= y && !pad->previous_swap;
            start |= menu && !pad->previous_start;
            pad->previous_swap = y;
            pad->previous_start = menu;
            int reset = rt && axis(p,SDL_CONTROLLER_AXIS_TRIGGERLEFT) > .5f &&
                     SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_BACK) &&
                     SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
            pad->reset_ms = reset ? pad->reset_ms+elapsed : 0;
            if (pad->reset_ms >= 1000) XReboot();
        }
        MatchInput input = {
            .move = move, .strafe = strafe, .turn = turn, .look = look,
            .fire = fire, .refill = refill, .swap = swap, .start = start,
            .assist_choice=tuned-classic, .jump=jump, .crouch=crouch
        };
        match_update(&input, elapsed, refreshes*refresh_period);
        match_view(&view);
        int aimed = match_targeted(&view);
        MatchSound effect = match_attack(&input);
        sound(effect,0);
        sound(view.bot_sound,1);
        match_view(&view);
        unsigned vblank=gpu_present(&view, &input, aimed, fps10, connected);
        refreshes=vblank-previous_vblank;
        previous_vblank=vblank;
        ++frames;
    }
}
