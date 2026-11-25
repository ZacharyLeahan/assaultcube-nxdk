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
#include "ambience.h"

extern const uint8_t shot_audio[] __asm__("_auto"), shot_end[] __asm__("_auto_end");
extern const uint8_t auto_reload[], auto_reload_end[];
extern const uint8_t usp[], usp_end[], pistol_reload[], pistol_reload_end[];
typedef struct { const uint8_t *start,*end; } Clip;
#define DECLARE_CLIP(n) extern const uint8_t n[], n##_end[]
DECLARE_CLIP(jump); DECLARE_CLIP(land); DECLARE_CLIP(pain1); DECLARE_CLIP(die1);
DECLARE_CLIP(footsteps); DECLARE_CLIP(crouch); DECLARE_CLIP(watersteps); DECLARE_CLIP(watercrouch);
DECLARE_CLIP(crouch_in); DECLARE_CLIP(crouch_out);
DECLARE_CLIP(change_gun);
DECLARE_CLIP(step);
DECLARE_CLIP(clip_empty);
DECLARE_CLIP(ric1); DECLARE_CLIP(ric2); DECLARE_CLIP(ric3); DECLARE_CLIP(waterimpact);
DECLARE_CLIP(pain2); DECLARE_CLIP(pain3); DECLARE_CLIP(pain4); DECLARE_CLIP(pain5);
DECLARE_CLIP(pain6); DECLARE_CLIP(die2);
DECLARE_CLIP(splash1); DECLARE_CLIP(splash2);
DECLARE_CLIP(pickup_ammo_clip); DECLARE_CLIP(pickup_health); DECLARE_CLIP(pickup_helmet); DECLARE_CLIP(pickup_armour);
#define CLIP(n) {n,n##_end}
static const Clip clips[] = {
    [RIFLE_SHOT]={shot_audio,shot_end}, [RIFLE_RELOAD]=CLIP(auto_reload),
    [WEAPON_CHANGE]=CLIP(change_gun),
    [BULLET_AIR1]=CLIP(ric1), [BULLET_AIR2]=CLIP(ric2),
    [BULLET_IMPACT]=CLIP(ric3), [BULLET_WATER]=CLIP(waterimpact),
    [WATER_ENTER]=CLIP(splash2), [WATER_EXIT]=CLIP(splash1),
    [PISTOL_SHOT]=CLIP(usp), [PISTOL_RELOAD]=CLIP(pistol_reload),
    [PLAYER_JUMP]=CLIP(jump), [PLAYER_LAND]=CLIP(land),
    [SOFT_LAND]=CLIP(step),
    [FOOTSTEPS]=CLIP(footsteps), [CROUCH_STEPS]=CLIP(crouch),
    [WATER_STEPS]=CLIP(watersteps), [WATER_CROUCH_STEPS]=CLIP(watercrouch),
    [CROUCH_IN]=CLIP(crouch_in), [CROUCH_OUT]=CLIP(crouch_out),
    [PLAYER_PAIN]=CLIP(pain6), [PLAYER_DEATH]=CLIP(die1), [ALT_DEATH]=CLIP(die2),
    [BOT_PAIN1]=CLIP(pain1), [BOT_PAIN2]=CLIP(pain2), [BOT_PAIN3]=CLIP(pain3),
    [BOT_PAIN4]=CLIP(pain4), [BOT_PAIN5]=CLIP(pain5), [PICKUP_AMMO]=CLIP(pickup_ammo_clip),
    [PICKUP_HEALTH]=CLIP(pickup_health), [PICKUP_HELMET]=CLIP(pickup_helmet), [PICKUP_ARMOUR]=CLIP(pickup_armour)
};
static SDL_AudioDeviceID audio_device;

enum { LOCAL_GUN, BOT_GUN, MOVEMENT, PICKUP, PAIN, BOT_VOICE, STANCE, AIR, IMPACT, LOCAL_STEPS, BOT_STEPS, EMPTY_CLICK };
typedef struct { const int16_t *data; unsigned remaining; int left,right; MatchSound loop; unsigned started; } Voice;
static const int volume[]={96,96,48,70,64,96,85,96,96,40,40,96};
static Voice voices[sizeof(volume)/sizeof(volume[0])];

static void set_footsteps(unsigned channel,MatchSound sound,unsigned now,int active) {
    Voice *v=&voices[channel];
    if (!active) { *v=(Voice){0}; return; }
    if (sound==v->loop) return;
    if (sound==SILENT) {
        if (now-v->started>=200) *v=(Voice){0};
        return;
    }
    const Clip *clip=&clips[sound];
    unsigned len=((uintptr_t)clip->end-(uintptr_t)clip->start)/2;
    unsigned offset=((now*2654435761u>>12)%500)*len/500;
    *v=(Voice){(const int16_t *)clip->start+offset,len-offset,volume[channel],volume[channel],sound,now};
}
static void mix_audio(void *unused, Uint8 *out, int len) {
    (void)unused;
    int16_t *stereo=(int16_t *)out;
    for (int frames=len/4;frames>0;) {
        int mix[256][2]={{0}};
        unsigned count=frames>256 ? 256 : (unsigned)frames;
        ambience_mix(mix,count);
        for (unsigned i=0;i<sizeof(voices)/sizeof(voices[0]);++i) {
            Voice *v=&voices[i];
            for (unsigned j=0;j<count && (v->remaining || v->loop);) {
                if (!v->remaining) {
                    const Clip *clip=&clips[v->loop];
                    v->data=(const int16_t *)clip->start;
                    v->remaining=((uintptr_t)clip->end-(uintptr_t)clip->start)/2;
                }
                unsigned n=v->remaining<count-j ? v->remaining : count-j;
                for (unsigned k=0;k<n;++k,++j) {
                    mix[j][0]+=v->data[k]*v->left;
                    mix[j][1]+=v->data[k]*v->right;
                }
                v->data+=n; v->remaining-=n;
            }
        }
        for (unsigned j=0;j<count;++j) for (int ch=0;ch<2;++ch) {
            int sample=mix[j][ch]/128;
            *stereo++=sample<-32768 ? -32768 : sample>32767 ? 32767 : sample;
        }
        frames-=count;
    }
}
static void spatial_gain(const MatchView *v,MatchPoint pos,float *left,float *right) {
    float dx=pos.x-v->px,dy=pos.y-v->py,dz=pos.z-v->eye;
    float distance=sqrtf(dx*dx+dy*dy+dz*dz);
    float pan=(dy*cosf(v->angle)-dx*sinf(v->angle))/fmaxf(.001f,distance);
    pan=fmaxf(-1,fminf(1,pan));
    float gain=4/fmaxf(4,distance);
    *left=gain*sqrtf(.5f*(1-pan)); *right=gain*sqrtf(.5f*(1+pan));
}
static void update_audio(const MatchView *v,MatchSound gun) {
    if (!audio_device) return;
    const MatchSound effects[]={gun,v->bot_sound,v->movement_sound,v->pickup_sound,v->pain_sound,v->bot_voice,v->stance_sound,v->air.sound,v->impact.sound};
    SDL_LockAudioDevice(audio_device);
    ambience_update(v->px,v->py,v->eye,v->angle);
    if (v->empty_click)
        voices[EMPTY_CLICK]=(Voice){(const int16_t *)clip_empty,
            ((uintptr_t)clip_empty_end-(uintptr_t)clip_empty)/2,volume[EMPTY_CLICK],volume[EMPTY_CLICK]};
    for (unsigned i=0;i<sizeof(effects)/sizeof(effects[0]);++i) {
        if (effects[i]==SILENT) continue;
        const Clip *clip=&clips[effects[i]];
        voices[i]=(Voice){(const int16_t *)clip->start,
            ((uintptr_t)clip->end-(uintptr_t)clip->start)/sizeof(int16_t),volume[i],volume[i]};
    }
    static MatchPoint origin[2];
    if (v->air.sound) origin[0]=v->air.pos;
    if (v->impact.sound) origin[1]=v->impact.pos;
    for (unsigned i=AIR;i<=IMPACT;++i) if (voices[i].remaining) {
        float left,right;
        spatial_gain(v,origin[i-AIR],&left,&right);
        voices[i].left=(int)(volume[i]*left+.5f); voices[i].right=(int)(volume[i]*right+.5f);
    }
    set_footsteps(LOCAL_STEPS,v->footsteps,v->elapsed_ms,v->phase==PLAYING && !v->respawning);
    set_footsteps(BOT_STEPS,v->bot_footsteps,v->elapsed_ms,v->phase==PLAYING && v->bot_alive);
    if (voices[BOT_GUN].remaining || voices[BOT_VOICE].remaining || voices[BOT_STEPS].loop) {
        float left,right;
        spatial_gain(v,(MatchPoint){v->tx,v->ty,v->bot_floor+PLAYER_EYE_HEIGHT},&left,&right);
        const int channels[]={BOT_GUN,BOT_VOICE,BOT_STEPS};
        for(unsigned i=0;i<3;++i) {
            int ch=channels[i];
            voices[ch].left=(int)(volume[ch]*left+.5f);
            voices[ch].right=(int)(volume[ch]*right+.5f);
        }
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
            fire |= rt;
            int x = SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_X);
            refill |= x && !pad->previous_reload;
            pad->previous_reload = x;
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
