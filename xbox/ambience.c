#include "ambience.h"
#include <math.h>

typedef struct { unsigned offset,frames,channels; } AmbientClip;
typedef struct { int x,y,z,slot,outer,inner,volume; } SoundEmitter;
#include "generated/ambience.h"
extern const unsigned char ambience[];
enum { EMITTER_COUNT=sizeof(emitters)/sizeof(emitters[0]) };
static struct { unsigned cursor; int left,right,playing; } voices[EMITTER_COUNT];

void ambience_update(float x,float y,float z,float yaw) {
    for (unsigned i=0;i<EMITTER_COUNT;++i) {
        const SoundEmitter *e=emitters+i;
        float dx=e->x-x,dy=e->y-y,dz=e->z-z;
        float d=sqrtf(dx*dx+dy*dy+dz*dz);
        if (e->outer && d>=e->outer) {
            voices[i].playing=0;
            voices[i].cursor=0;
            continue;
        }
        float left=1,right=1;
        if (e->inner) {
            if (d>e->inner)
                left=right=(e->outer-d)/(e->outer-e->inner);
            if (ambient_clips[e->slot].channels==1) {
                left*=.70710678f; right*=.70710678f;
            }
        } else {
            float pan=(dy*cosf(yaw)-dx*sinf(yaw))/fmaxf(.001f,d);
            pan=fminf(1,fmaxf(-1,pan));
            float gain=4/fmaxf(4,d);
            left=gain*sqrtf(.5f*(1-pan));
            right=gain*sqrtf(.5f*(1+pan));
        }
        // The hawk's map volume is 3/255. Keep fractional gain as it fades.
        float volume=32768*(e->volume?e->volume:255)/255.f;
        voices[i].left=(int)(left*volume+.5f);
        voices[i].right=(int)(right*volume+.5f);
        voices[i].playing=1;
    }
}

void ambience_mix(int (*mix)[2],unsigned frames) {
    for (unsigned i=0;i<EMITTER_COUNT;++i) {
        if (!voices[i].playing) continue;
        const AmbientClip *clip=ambient_clips+emitters[i].slot;
        unsigned cursor=voices[i].cursor;
        const unsigned char *base=ambience+clip->offset;
        for (unsigned j=0;j<frames;++j) {
            const unsigned char *p=base+cursor*clip->channels;
            mix[j][0]+=(decoded[p[0]]*voices[i].left)/256;
            mix[j][1]+=(decoded[p[clip->channels-1]]*voices[i].right)/256;
            if (++cursor==clip->frames) cursor=0;
        }
        voices[i].cursor=cursor;
    }
}

static struct { unsigned phase; int active; } listener[2];

void ambience_listener(int underwater,int ringing) {
    listener[0].active=underwater;
    listener[1].active=ringing;
    for(unsigned i=0;i<2;++i) if(!listener[i].active) listener[i].phase=0;
}
void ambience_listener_mix(int (*mix)[2],unsigned frames) {
    static const unsigned clip_index[]={UNDERWATER_CLIP,TINNITUS_CLIP};
    // OpenAL pitches the underwater loop to 0.65 and ringing to 1.9.
    static const unsigned step[]={13,38},gain[]={100,2};
    for(unsigned i=0;i<2;++i) if(listener[i].active) {
        const AmbientClip *clip=&ambient_clips[clip_index[i]];
        const unsigned char *base=ambience+clip->offset;
        unsigned phase=listener[i].phase;
        for(unsigned j=0;j<frames;++j) {
            unsigned index=phase/20,frac=phase%20;
            int first=decoded[base[index]],next=decoded[base[index+1==clip->frames?0:index+1]];
            int sample=(first+(next-first)*(int)frac/20)*(int)gain[i];
            mix[j][0]+=sample;mix[j][1]+=sample;
            phase+=step[i];
            if(phase>=clip->frames*20) phase-=clip->frames*20;
        }
        listener[i].phase=phase;
    }
}
