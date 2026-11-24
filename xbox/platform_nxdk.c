#include <SDL.h>
#include <hal/debug.h>
#include <hal/video.h>
#include <hal/xbox.h>
#include <pbkit/pbkit.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "render.h"
#include "controllers.h"

extern const uint8_t shot_audio[] __asm__("_auto"), shot_end[] __asm__("_auto_end");
extern const uint8_t auto_reload[], auto_reload_end[];
extern const uint8_t usp[], usp_end[], pistol_reload[], pistol_reload_end[];
static MatchFrame frame;
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

int main(void) {
    XVideoSetMode(640,480,32,REFRESH_DEFAULT);
    if (SDL_Init(SDL_INIT_GAMECONTROLLER) || pb_init()) { debugPrint("AssaultCube init failed"); return 1; }
    pb_show_front_screen();
    if (SDL_InitSubSystem(SDL_INIT_AUDIO)==0) {
        SDL_AudioSpec spec = {0};
        spec.freq = 48000; spec.format = AUDIO_S16LSB; spec.channels = 1; spec.samples = 1024; spec.callback = mix_audio;
        audio_device = SDL_OpenAudioDevice(NULL,0,&spec,NULL,0);
        if (audio_device) SDL_PauseAudioDevice(audio_device,0);
    }
    Pad pads[PAD_COUNT] = {0};
    unsigned last = SDL_GetTicks();
    MatchView view = {0};
    match_prepare();
    for (;;) {
        SDL_PumpEvents();
        SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
        refresh_pads(pads);
        unsigned now = SDL_GetTicks(), elapsed = now-last;
        last = now;
        float move = 0, strafe = 0, turn = 0, look = 0;
        int swap = 0, fire = 0, refill = 0, start = 0, connected = 0;
        for (int i = 0; i < PAD_COUNT; ++i) if (pads[i].handle) {
            Pad *pad = &pads[i];
            SDL_GameController *p = pad->handle;
            ++connected;
            move -= axis(p,SDL_CONTROLLER_AXIS_LEFTY); strafe += axis(p,SDL_CONTROLLER_AXIS_LEFTX);
            turn += axis(p,SDL_CONTROLLER_AXIS_RIGHTX); look -= axis(p,SDL_CONTROLLER_AXIS_RIGHTY);
            int rt = axis(p,SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > .25f;
            fire |= rt; refill |= SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_X);
            int y = SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_Y);
            int menu = SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_START);
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
            .fire = fire, .refill = refill, .swap = swap, .start = start
        };
        match_update(&input, elapsed);
        match_view(&view);
        int aimed = render_scene(&view, frame);
        MatchSound effect = match_attack(&input, aimed);
        sound(effect,0);
        sound(view.bot_sound,1);
        match_view(&view);
        render_overlay(&view, aimed, frame);
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
        pb_print("\n   ASSAULTCUBE   /   ac_desert   /   BOT DM\n");
        if (view.phase == READY) {
            pb_print("   Press START   /   first to %d or 90 seconds\n", FRAG_LIMIT);
            pb_print("   RT fire    X reload    Y weapon\n");
        } else {
            pb_print("   YOU %d   BOT %d     %u:%02u\n",view.player_frags,view.bot_frags,view.seconds/60,view.seconds%60);
            if (view.phase == INTERMISSION) {
                pb_print("   %s    START for rematch\n", view.player_frags > view.bot_frags ? "YOU WIN" :
                         view.player_frags < view.bot_frags ? "BOT WINS" : "DRAW");
            } else if (view.respawning) pb_print("   FRAGGED    Respawning...\n");
            else pb_print("   HP %d   ARM %d   %s %d/%d  %s\n",view.health,view.armour,
                          view.pistol ? "MK-77" : "MTP-57", view.magazine,view.reserve,
                          view.reload ? "RELOAD" : view.hurt ? "HIT" : "");
        }
        if (!connected) pb_print("   Connect an Xbox controller\n");
        else if (view.phase != PLAYING) pb_print("   Controllers connected: %d\n",connected);
        pb_draw_text_screen();
        while (pb_busy()) {}
        while (pb_finished()) {}
    }
}
