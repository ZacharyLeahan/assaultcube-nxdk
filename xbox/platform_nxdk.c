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
typedef struct { const uint8_t *start,*end; } Clip;
#define DECLARE_CLIP(n) extern const uint8_t n[], n##_end[]
DECLARE_CLIP(jump); DECLARE_CLIP(land); DECLARE_CLIP(step); DECLARE_CLIP(pain1); DECLARE_CLIP(die1);
DECLARE_CLIP(change_gun);
DECLARE_CLIP(pain2); DECLARE_CLIP(pain3); DECLARE_CLIP(pain4); DECLARE_CLIP(pain5);
DECLARE_CLIP(pain6); DECLARE_CLIP(die2);
DECLARE_CLIP(pickup_ammo_clip); DECLARE_CLIP(pickup_health); DECLARE_CLIP(pickup_helmet); DECLARE_CLIP(pickup_armour);
#define CLIP(n) {n,n##_end}
static const Clip clips[] = {
    [RIFLE_SHOT]={shot_audio,shot_end}, [RIFLE_RELOAD]=CLIP(auto_reload),
    [WEAPON_CHANGE]=CLIP(change_gun),
    [PISTOL_SHOT]=CLIP(usp), [PISTOL_RELOAD]=CLIP(pistol_reload),
    [PLAYER_JUMP]=CLIP(jump), [PLAYER_LAND]=CLIP(land), [PLAYER_STEP]=CLIP(step),
    [PLAYER_PAIN]=CLIP(pain6), [PLAYER_DEATH]=CLIP(die1), [ALT_DEATH]=CLIP(die2),
    [BOT_PAIN1]=CLIP(pain1), [BOT_PAIN2]=CLIP(pain2), [BOT_PAIN3]=CLIP(pain3),
    [BOT_PAIN4]=CLIP(pain4), [BOT_PAIN5]=CLIP(pain5), [PICKUP_AMMO]=CLIP(pickup_ammo_clip),
    [PICKUP_HEALTH]=CLIP(pickup_health), [PICKUP_HELMET]=CLIP(pickup_helmet), [PICKUP_ARMOUR]=CLIP(pickup_armour)
};
static SDL_AudioDeviceID audio_device;

enum { LOCAL_GUN, BOT_GUN, MOVEMENT, PICKUP, PAIN, BOT_VOICE };
typedef struct { const int16_t *data; unsigned remaining; int left,right; } Voice;
static const int volume[]={96,96,48,70,64,96};
static Voice voices[sizeof(volume)/sizeof(volume[0])];

static void mix_audio(void *unused, Uint8 *out, int len) {
    (void)unused;
    int16_t *stereo=(int16_t *)out;
    for (int frames=len/4;frames>0;) {
        int mix[256][2]={{0}};
        unsigned count=frames>256 ? 256 : (unsigned)frames;
        for (unsigned i=0;i<sizeof(voices)/sizeof(voices[0]);++i) {
            Voice *v=&voices[i];
            unsigned n=v->remaining<count ? v->remaining : count;
            if (!n) continue;
            for (unsigned j=0;j<n;++j) {
                mix[j][0]+=v->data[j]*v->left;
                mix[j][1]+=v->data[j]*v->right;
            }
            v->data+=n; v->remaining-=n;
        }
        for (unsigned j=0;j<count;++j) for (int ch=0;ch<2;++ch) {
            int sample=mix[j][ch]/128;
            *stereo++=sample<-32768 ? -32768 : sample>32767 ? 32767 : sample;
        }
        frames-=count;
    }
}
static void update_audio(const MatchView *v,MatchSound gun) {
    if (!audio_device) return;
    const MatchSound effects[]={gun,v->bot_sound,v->movement_sound,v->pickup_sound,v->pain_sound,v->bot_voice};
    SDL_LockAudioDevice(audio_device);
    for (unsigned i=0;i<sizeof(effects)/sizeof(effects[0]);++i) {
        if (effects[i]==SILENT) continue;
        const Clip *clip=&clips[effects[i]];
        voices[i]=(Voice){(const int16_t *)clip->start,
            ((uintptr_t)clip->end-(uintptr_t)clip->start)/sizeof(int16_t),volume[i],volume[i]};
    }
    if (voices[BOT_GUN].remaining || voices[BOT_VOICE].remaining) {
        float dx=v->tx-v->px,dy=v->ty-v->py;
        float dz=map_floor(map_cube((int)v->tx,(int)v->ty))+3.6f-v->eye;
        float distance=sqrtf(dx*dx+dy*dy+dz*dz);
        float pan=(dy*cosf(v->angle)-dx*sinf(v->angle))/fmaxf(.001f,distance);
        pan=fmaxf(-1,fminf(1,pan));
        // Match the PC's inverse-distance rolloff with a four-cube reference distance.
        float gain=volume[BOT_GUN]*4/fmaxf(4,distance);
        voices[BOT_GUN].left=(int)(gain*sqrtf(.5f*(1-pan))+.5f);
        voices[BOT_GUN].right=(int)(gain*sqrtf(.5f*(1+pan))+.5f);
        voices[BOT_VOICE].left=voices[BOT_GUN].left;
        voices[BOT_VOICE].right=voices[BOT_GUN].right;
    }
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
        spec.freq = 48000; spec.format = AUDIO_S16LSB; spec.channels = 2; spec.samples = 1024; spec.callback = mix_audio;
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
        gpu_begin_input(refresh_hz,view.respawning,view.bot_shot_pending);
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
        int jump=0, crouch=0, showmap=0;
        int swap = 0, fire = 0, refill = 0, start = 0, connected = 0;
        for (int i = 0; i < PAD_COUNT; ++i) if (pads[i].handle) {
            Pad *pad = &pads[i];
            SDL_GameController *p = pad->handle;
            ++connected;
            move -= axis(p,SDL_CONTROLLER_AXIS_LEFTY); strafe += axis(p,SDL_CONTROLLER_AXIS_LEFTX);
            read_aim(p,&turn,&look);
            jump |= SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_A);
            crouch |= SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_B);
            showmap |= SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_BACK);
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
            .assist_choice=tuned-classic, .jump=jump, .crouch=crouch, .showmap=showmap
        };
        match_update(&input, elapsed, refreshes*refresh_period);
        match_view(&view);
        int aimed = match_targeted(&view);
        MatchSound effect = match_attack(&input);
        match_view(&view);
        update_audio(&view,effect);
        unsigned vblank=gpu_present(&view, &input, aimed, fps10, connected);
        refreshes=vblank-previous_vblank;
        previous_vblank=vblank;
        ++frames;
    }
}
