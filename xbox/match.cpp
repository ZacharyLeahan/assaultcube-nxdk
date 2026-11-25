#include "match.h"
#include "geometry.h"
#include "navigation.h"
#include <math.h>
#include <string.h>
#include "../source/src/playerstate.h"
#define STANDALONE
typedef unsigned char uchar;
#include "../source/src/sound.h"
#include "../source/src/weaponstats.inc"

namespace {
struct ItemLocation { int x, y, height_tenths, kind; };
#include "generated/starts.h"
#include "generated/items.h"
#include "generated/water.h"
#include "generated/bot_head.h"
constexpr int start_count = sizeof(starts)/sizeof(starts[0]);
constexpr int item_count = sizeof(locations)/sizeof(locations[0]);
constexpr unsigned duration = 90000;
constexpr unsigned switch_ms = 400;
constexpr float radians = .017453293f;
struct Fighter {
    playerstate state;
    float x, y, yaw, pitch, eye_height;
    float velocity_x, velocity_y, turn_boost;
    float sway_x, sway_y;
    float pitch_velocity;
    unsigned shots, shot_until;
    int turn_direction;
    int frags;
    unsigned respawn_at, ready_at, reload_at, flash_until, pain_until;
};
Fighter player, bot;
float feet_z, stance, jump_speed, sway_vertical;
unsigned air_ms,jump_start_ticks;
unsigned sway_ms;
bool grounded, held_jump, in_water;
unsigned last_splash;
unsigned clock_ms, frame_start_ms, next_route, bot_attack_at, hurt_until;
unsigned damage_directions[8];
unsigned unavailable_until[item_count], random_state;
int burst, bot_pose;
float stride_phase;
unsigned idle_ms;
bool pistol_trigger_used,empty_click,bot_saw_player;
unsigned switch_end;
unsigned intermission_age;
int next_gun;
MatchPhase phase;
bool classic_assist;
MatchSound bot_sound;
MatchSound movement_sound, pickup_sound, pain_sound, bot_voice;
MatchSound step_sound,stance_sound;
PositionalSound air_sound,impact_sound;
ShotLine bot_line;
bool was_crouching;
ItemView item_views[item_count];
BulletMark marks[64];
unsigned next_mark, mark_count;
SplashParticle sparks[64];
BloodParticle blood[64];
unsigned next_blood,blood_count;
unsigned next_spark,spark_count,effect_seed;
unsigned effect_random(unsigned limit) {
    effect_seed=1664525u*effect_seed+1013904223u;
    return (effect_seed>>8)%limit;
}
SplashParticle splash_particle(float x,float y,float z,unsigned max_lifetime) {
    short dx,dy,dz;
    do {
        dx=(int)effect_random(300)-150;
        dy=(int)effect_random(300)-150;
        dz=(int)effect_random(300)-150;
    } while (dx*dx+dy*dy+dz*dz>150*150);
    return {x,y,z,dx,dy,dz,clock_ms,effect_random(max_lifetime)};
}
void spark_splash(float x,float y,float z) {
    for (int i=0;i<5;++i) {
        sparks[next_spark]=splash_particle(x,y,z,750);
        next_spark=(next_spark+1)%64;
        if (spark_count<64) ++spark_count;
    }
}
void mark_surface(const SurfaceHit &hit) {
    if (!hit.can_mark) return;
    marks[next_mark]={hit.x,hit.y,hit.z,hit.axis,clock_ms+10000};
    next_mark=(next_mark+1)%64;
    if (mark_count<64) ++mark_count;
}
void update_blood(unsigned elapsed) {
    for (unsigned i=0;i<blood_count;++i) {
        BloodParticle &b=blood[i];
        SplashParticle &p=b.motion;
        unsigned age=clock_ms-p.born;
        if (b.on_floor || age>p.lifetime) continue;
        float travel=elapsed/20000.f;
        p.x+=travel*p.dx; p.y+=travel*p.dy;
        p.z+=travel*p.dz-age*(float)elapsed/30000.f;
        const Cube *c=map_cube((int)p.x,(int)p.y);
        if (c->kind==4 && p.z<=c->floor) {
            p.z=c->floor+.005f; p.born=clock_ms; p.lifetime=10000;
            b.on_floor=1;
        }
    }
}
unsigned random_number() {
    random_state = random_state*1664525u + 1013904223u;
    return random_state >> 8;
}
float ground(float x, float y) { return map_floor(map_cube((int)x,(int)y)); }
float distance(float x,float y,float tx,float ty) { return hypotf(tx-x,ty-y); }
float eye(const Fighter &f) { return f.eye_height; }
void spawn(Fighter &f, int index) {
    f.x = starts[index][0]+.5f; f.y = starts[index][1]+.5f;
    f.yaw = (starts[index][2]*.1f-90)*radians; f.pitch = 0;
    f.eye_height=body_floor(f.x,f.y)+PLAYER_EYE_HEIGHT;
    if (&f==&player) {
        switch_end=0;
        memset(damage_directions,0,sizeof(damage_directions));
        hurt_until=0;
        feet_z=f.eye_height-PLAYER_EYE_HEIGHT; stance=PLAYER_EYE_HEIGHT; jump_speed=0; air_ms=jump_start_ticks=0;
        grounded=true; held_jump=false;
        in_water=false; last_splash=0;
        sway_ms=0; sway_vertical=0;
        was_crouching=false; step_sound=stance_sound=SILENT;
    }
    f.velocity_x=f.velocity_y=f.turn_boost=0;
    f.sway_x=f.sway_y=0;
    f.pitch_velocity=0; f.shots=f.shot_until=0;
    f.turn_direction=0;
    f.state.respawn();
    f.state.spawnstate(GMODE_BOTDEATHMATCH);
    f.respawn_at = f.ready_at = f.reload_at = f.flash_until = f.pain_until = 0;
}
void aim_at_opponent(Fighter &f, const Fighter &opponent) {
    f.yaw = atan2f(opponent.y-f.y,opponent.x-f.x);
    f.pitch = atan2f(eye(opponent)-1-eye(f),fmaxf(1,distance(f.x,f.y,opponent.x,opponent.y)));
}
void begin(MatchPhase initial) {
    next_mark=mark_count=0;
    bot_line={};
    intermission_age=0;
    next_spark=spark_count=0; effect_seed=1;
    next_blood=blood_count=0;
    clock_ms = frame_start_ms = 0; random_state = 7331; phase = initial;
    player.frags = bot.frags = burst = bot_pose = 0;
    stride_phase=0; idle_ms=0;
    next_route = hurt_until = 0;
    bot_attack_at = 1500; bot_saw_player = pistol_trigger_used = empty_click = false;
    memset(unavailable_until,0,sizeof(unavailable_until));
    // Open in the same courtyard at two official free-for-all player starts.
    spawn(player,17); spawn(bot,18);
    aim_at_opponent(player,bot); aim_at_opponent(bot,player);
}
void respawn_near(Fighter &f, const Fighter &opponent) {
    // Sample FFA spawns, preferring covered positions with a route back to combat.
    route_to((int)opponent.x,(int)opponent.y);
    int best = -1, fallback = -1;
    float best_score = -100000;
    int first = random_number()%start_count;
    for (int n = 0; n < start_count; ++n) {
        int i = (n+first)%start_count;
        float x = starts[i][0]+.5f, y = starts[i][1]+.5f, tx, ty;
        float d = distance(x,y,opponent.x,opponent.y);
        if (d < 12) continue;
        if (fallback < 0) fallback = i;
        if (!route_next(x,y,&tx,&ty)) continue;
        float score = -fabsf(d-32);
        float spawn_eye=body_floor(x,y)+PLAYER_EYE_HEIGHT;
        if (!visible(x,y,spawn_eye,opponent.x,opponent.y,eye(opponent))) score += 8;
        if (score > best_score) { best_score = score; best = i; }
    }
    spawn(f,best >= 0 ? best : fallback);
    aim_at_opponent(f,opponent);
    next_route = 0; bot_attack_at = clock_ms+1200; burst = 0;
    bot_saw_player = false;
}
void impulse(Fighter &f,float x,float y,float z,float amount) {
    f.velocity_x+=amount*x;
    f.velocity_y+=amount*y;
    if (&f==&player && (!grounded || in_water)) {
        float speed=(stance<PLAYER_EYE_HEIGHT?6.4f:16.f)*(in_water?.5f:1.f);
        jump_speed+=amount*z*speed;
    }
}
void firing_impulse(Fighter &f,int gun,float dx,float dy,float dz,bool crouched) {
    float recoil=guns[gun].recoil-(gun==GUN_ASSAULT ? effect_random(8)*.01f : 0.f);
    impulse(f,dx,dy,dz,-.01f*recoil*(crouched?.75f:1));
}
void damage(Fighter &victim, Fighter &attacker, int gun,float dx,float dy,float dz) {
    float length=sqrtf(dx*dx+dy*dy+dz*dz);
    if (length>0) impulse(victim,dx/length,dy/length,dz/length,guns[gun].damage*.01f*guns[gun].pushfactor);
    int dealt=victim.state.dodamage(guns[gun].damage,gun);
    victim.pain_until=clock_ms+300;
    bool dead=victim.state.health<=0;
    MatchSound death=dead && effect_random(2) ? ALT_DEATH : PLAYER_DEATH;
    if (&victim == &player) {
        unsigned remaining=hurt_until>clock_ms?hurt_until-clock_ms:0;
        hurt_until=clock_ms+fminf(3999,remaining+7*dealt);
        float relative=atan2f(attacker.y-player.y,attacker.x-player.x)-player.yaw;
        int sector=(int)floorf(relative/(45*radians)+.5f);
        damage_directions[(unsigned)sector%8]=clock_ms+1000;
        pain_sound=dead ? death : PLAYER_PAIN;
    } else {
        bot_voice=dead ? death : (MatchSound)(BOT_PAIN1+effect_random(5));
        for (int i=0;i<guns[gun].damage/10;++i) {
            blood[next_blood]={splash_particle(victim.x,victim.y,eye(victim),3000),0};
            next_blood=(next_blood+1)%64;
            if (blood_count<64) ++blood_count;
        }
    }
    if (victim.state.health > 0) return;
    victim.state.health = 0;
    victim.respawn_at = clock_ms+2000;
    victim.flash_until = 0;
    ++attacker.frags;
    if (attacker.frags >= FRAG_LIMIT) phase = INTERMISSION;
}
MatchSound trigger(Fighter &f, bool fire, bool refill) {
    int gun = f.state.gunselect;
    int &mag = f.state.mag[gun], &reserve = f.state.ammo[gun];
    if (f.respawn_at) return SILENT;
    bool manual=refill && mag<magsize(gun) && reserve>0 && f.reload_at<=clock_ms;
    if (!manual && f.ready_at>clock_ms) return SILENT;
    bool dry=&f==&player && fire && !mag && !manual;
    if (dry) {
        empty_click=true;
        f.shots=0;
        f.ready_at=clock_ms+250;
    }
    if (manual || (!mag && reserve>0 && (fire || &f==&player))) {
        int count = magsize(gun)-mag;
        if (count > reserve) count = reserve;
        mag += count; reserve -= count;
        // Manual reload starts immediately, retaining any outstanding shot wait.
        unsigned start=f.ready_at>clock_ms?f.ready_at:clock_ms;
        f.ready_at=start+reloadtime(gun);
        f.reload_at=clock_ms+reloadtime(gun);
        return gun == GUN_PISTOL ? PISTOL_RELOAD : RIFLE_RELOAD;
    }
    if (!fire || !mag) return SILENT;
    --mag;
    // Preserve the part of this frame spent past the previous shot deadline.
    // An idle trigger or a long stall starts a fresh firing interval.
    unsigned delay=attackdelay(gun),action=clock_ms;
    if (f.ready_at && f.ready_at>=frame_start_ms && clock_ms-f.ready_at<delay)
        action=f.ready_at;
    f.ready_at = action+delay;
    f.flash_until = action+90;
    return gun == GUN_PISTOL ? PISTOL_SHOT : RIFLE_SHOT;
}
void collect(Fighter &f) {
    if (f.respawn_at) return;
    for (int i = 0; i < item_count; ++i) {
        const ItemLocation &p = locations[i];
        float dz=(&f==&player?feet_z:f.eye_height-PLAYER_EYE_HEIGHT)-ground(p.x,p.y)-p.height_tenths*.1f;
        float dx=f.x-p.x, dy=f.y-p.y;
        if (unavailable_until[i] > clock_ms || dx*dx+dy*dy+dz*dz>=6.25f ||
            !f.state.canpickup(p.kind)) continue;
        f.state.pickup(p.kind);
        if (&f==&player) {
            pickup_sound=p.kind==I_HEALTH?PICKUP_HEALTH:p.kind==I_HELMET?PICKUP_HELMET:
                         p.kind==I_ARMOUR?PICKUP_ARMOUR:PICKUP_AMMO;
        }
        unsigned delay = p.kind == I_HEALTH ? 20000 : (p.kind == I_HELMET || p.kind == I_ARMOUR) ? 25000 : 8000;
        unavailable_until[i] = clock_ms+delay;
    }
}
void bot_shot_effects(float yaw,bool hit_player) {
    MatchPoint from={bot.x,bot.y,eye(bot)}, target={player.x,player.y,eye(player)-.8f};
    float d=fmaxf(.01f,distance(from.x,from.y,target.x,target.y));
    float dx=cosf(yaw),dy=sinf(yaw),dz=(target.z-from.z)/d;
    if (!hit_player) {
        target={from.x+512*dx,from.y+512*dy,from.z+512*dz};
        SurfaceHit hit;
        if (trace_surface(from.x,from.y,from.z,target.x,target.y,target.z,&hit)) {
            target={hit.x,hit.y,hit.z};
            mark_surface(hit);
            spark_splash(hit.x,hit.y,hit.z);
            if (hit.can_mark && hypotf(distance(hit.x,hit.y,player.x,player.y),hit.z-eye(player))<=15)
                impact_sound={target,hit.z<WATER_LEVEL?BULLET_WATER:BULLET_IMPACT};
        }
    }
    dx=target.x-from.x; dy=target.y-from.y; dz=target.z-from.z;
    float length=sqrtf(dx*dx+dy*dy+dz*dz);
    if (length<1) return;
    auto point=[&](float t) -> MatchPoint { return {from.x+t*dx,from.y+t*dy,from.z+t*dz}; };
    float fd=hypotf(d,from.z-eye(player));
    float td=hypotf(distance(target.x,target.y,player.x,player.y),target.z-eye(player));
    float start=td<=10?8:5;
    bot_line={point(.1f+start/length),point((start+2)*.1f),clock_ms+75};
    if (fd<=8 || td<=8) return;
    MatchPoint near=point(fd/(fd+td));
    if (hypotf(distance(near.x,near.y,player.x,player.y),near.z-eye(player))<=15)
        air_sound={near,effect_random(2)?BULLET_AIR1:BULLET_AIR2};
}
void update_bot(float dt) {
    bot_pose = 0;
    // Bot navigation supplies its own walk speed. Carry shot impulses through
    // the same ground friction used by the player, including during a fall pose.
    if (fabsf(bot.velocity_x)+fabsf(bot.velocity_y)>.0001f) {
        float retain=powf(23.f/24,dt/.005f);
        float travel=16*.115f*(1-retain);
        walk(&bot.x,&bot.y,bot.velocity_x*travel,bot.velocity_y*travel);
        bot.eye_height=body_floor(bot.x,bot.y)+PLAYER_EYE_HEIGHT;
        bot.velocity_x*=retain; bot.velocity_y*=retain;
    }
    if (bot.respawn_at || player.respawn_at || clock_ms < 1000) return;
    float dx = player.x-bot.x, dy = player.y-bot.y;
    float d = fmaxf(.01f,hypotf(dx,dy));
    bool sees = visible(bot.x,bot.y,eye(bot),player.x,player.y,eye(player)-.8f);
    float mx = 0, my = 0;
    if (!sees || d > 23) {
        if (clock_ms >= next_route) {
            route_to((int)player.x,(int)player.y);
            next_route = clock_ms+450;
        }
        float tx, ty;
        if (route_next(bot.x,bot.y,&tx,&ty)) {
            mx = tx-bot.x; my = ty-bot.y;
        }
    }
    float length = hypotf(mx,my), old_x = bot.x, old_y = bot.y;
    if (length > .001f) {
        float step = fminf(length,(sees ? 4 : 11)*dt);
        walk(&bot.x,&bot.y,mx/length*step,my/length*step);
        bot.eye_height=body_floor(bot.x,bot.y)+PLAYER_EYE_HEIGHT;
        float traveled=distance(old_x,old_y,bot.x,bot.y);
        if (traveled>.001f) {
            // Advance the six original run poses by distance to avoid foot sliding.
            stride_phase=fmodf(stride_phase+traveled*1.5f,6.f);
            bot_pose=1+(int)stride_phase;
        }
    }
    if (!sees) { bot_saw_player = false; burst = 0; return; }
    if (!bot_saw_player) {
        // SKILL5 in bot/bot_skill.cfg allows 1.5-2 seconds to acquire a target.
        bot_attack_at = clock_ms+1500+random_number()%501;
        bot_saw_player = true;
    }
    bot.pitch=atan2f(eye(player)-.8f-eye(bot),d);
    float ideal = atan2f(player.y-bot.y,player.x-bot.x);
    float delta = atan2f(sinf(ideal-bot.yaw),cosf(ideal-bot.yaw));
    bot.yaw += fmaxf(-75*radians*dt,fminf(75*radians*dt,delta));
    if (clock_ms < bot_attack_at || clock_ms<bot.ready_at || fabsf(delta)>.14f || d>80) return;
    int gun = bot.state.gunselect;
    if (!bot.state.mag[gun] && !bot.state.ammo[gun]) bot.state.gunselect = GUN_PISTOL;
    if (!burst) burst = 1+random_number()%3;
    // Local bot bursts use the same magazine, reload, damage and armour path as the player.
    float error = ((int)(random_number()%201)-100)*.00055f;
    bool on_target = fabsf(delta+error)*d < .95f &&
        visible(bot.x,bot.y,eye(bot),player.x,player.y,eye(player)-.8f);
    MatchSound shot = trigger(bot,true,false);
    if (shot == RIFLE_SHOT || shot == PISTOL_SHOT) {
        int fired=bot.state.gunselect;
        float yaw=ideal-delta-error,pitch=atan2f(eye(player)-.8f-eye(bot),d);
        firing_impulse(bot,fired,cosf(pitch)*cosf(yaw),cosf(pitch)*sinf(yaw),sinf(pitch),false);
        if (on_target) damage(player,bot,fired,player.x-bot.x,player.y-bot.y,eye(player)-.8f-eye(bot));
        bot_shot_effects(yaw,on_target);
        if (--burst == 0) bot_attack_at = clock_ms+1500+random_number()%501;
    }
    bot_sound = shot;
}
}

void match_prepare(void) { navigation_prepare(); begin(READY); }
void match_update(const MatchInput *in, unsigned elapsed, float motion_seconds) {
    if (phase!=INTERMISSION) idle_ms=(idle_ms+elapsed)%4000;
    bot_sound=movement_sound=pickup_sound=pain_sound=bot_voice=SILENT;
    step_sound=stance_sound=SILENT;
    air_sound.sound=impact_sound.sound=SILENT;
    if (phase != PLAYING) {
        if (phase==INTERMISSION && intermission_age<2000) intermission_age+=elapsed;
        if (in->assist_choice) classic_assist=in->assist_choice<0;
        if (in->start) begin(PLAYING);
        return;
    }
    frame_start_ms=clock_ms;
    clock_ms += elapsed;
    update_blood(elapsed);
    if (clock_ms >= duration) { phase = INTERMISSION; return; }
    if (player.respawn_at && player.respawn_at <= clock_ms) respawn_near(player,bot);
    if (bot.respawn_at && bot.respawn_at <= clock_ms) respawn_near(bot,player);
    float dt = fminf(.10f,motion_seconds);
    float old_dx=bot.x-player.x, old_dy=bot.y-player.y;
    float manual_yaw=0;
    if (!player.respawn_at) {
        float turn=fmaxf(-1,fminf(1,in->turn));
        int direction=(turn>0)-(turn<0);
        if (direction!=player.turn_direction || fabsf(turn)<.85f) player.turn_boost=0;
        player.turn_direction=direction;
        if (fabsf(turn)>=.85f) player.turn_boost=fminf(1,player.turn_boost+dt/.4f);
        float outer=fmaxf(0,(fabsf(turn)-.85f)/.15f);
        float yaw_rate=turn*(150+70*outer*player.turn_boost)*radians;
        manual_yaw=yaw_rate*dt;
        player.yaw=fmodf(player.yaw+manual_yaw,6.2831853f);
        float look=fmaxf(-1,fminf(1,in->look));
        float pitch_change=look*105*dt;
        // The PC applies recoil on a 5 ms physics clock. Keep stick input on
        // the display clock so a new aim report affects this rendered frame.
        unsigned steps=(clock_ms+4)/5-(frame_start_ms+4)/5;
        float step_gain=.005f*16*(in->crouch?.75f:1);
        for (unsigned i=0;i<steps && i<20 && player.pitch_velocity!=0;++i) {
            pitch_change+=player.pitch_velocity*step_gain;
            player.pitch_velocity*=.875f;
            if (player.pitch_velocity>.001f && player.pitch_velocity<.05f)
                player.pitch_velocity-=guns[player.state.gunselect].recoilbackfade*.01f;
            if (fabsf(player.pitch_velocity)<.000001f) player.pitch_velocity=0;
        }
        player.pitch=fmaxf(-75*radians,fminf(75*radians,player.pitch+pitch_change*radians));

        float floor,ceiling;
        body_space(player.x,player.y,feet_z,&floor,&ceiling);
        float ladder_top=0;
        bool climbing=!in->crouch && ladder_space(player.x,player.y,feet_z,&ladder_top);
        float target=fminf(in->crouch?.75f*PLAYER_EYE_HEIGHT:PLAYER_EYE_HEIGHT,ceiling-feet_z-PLAYER_ABOVE_EYE);
        float stance_step=.6f*16*dt*(player.eye_height<WATER_LEVEL?.5f:1.f);
        stance+=fmaxf(-stance_step,fminf(stance_step,target-stance));
        bool swimming=player.eye_height<WATER_LEVEL+.5f;
        if (swimming && !in_water) jump_speed=0;
        if ((grounded || swimming) && !climbing && !held_jump && in->jump && !in->crouch && stance>=PLAYER_EYE_HEIGHT) {
            grounded=false; jump_speed=swimming?16:32; air_ms=0;
            jump_start_ticks=swimming?0:2;
            if (swimming) { player.velocity_x/=8; player.velocity_y/=8; }
            else movement_sound=PLAYER_JUMP;
        }
        held_jump=in->jump;
        float speed=stance<PLAYER_EYE_HEIGHT?6.4f:16.f;
        if (swimming) speed*=.5f;
        float c=cosf(player.yaw), s=sinf(player.yaw);
        float forward=climbing && !grounded && in->move<0 ? 0 : in->move;
        if (swimming) forward*=cosf(player.pitch);
        float stick_length=fmaxf(1,hypotf(in->move,in->strafe));
        float target_x=(forward*c-in->strafe*s)/stick_length;
        float target_y=(forward*s+in->strafe*c)/stick_length;
        // Sum the PC's 5 ms friction steps, including distance travelled
        // during acceleration, over the current display interval.
        float friction=swimming?20.f:grounded?6.f:climbing?1.5f:30.f;
        float retain=1-.005f/(friction*.02f);
        float fraction=1-powf(retain,dt/.005f);
        float carry=(friction*.02f-.005f)*fraction;
        float dx=player.velocity_x-target_x,dy=player.velocity_y-target_y;
        float travel_x=(target_x*dt+dx*carry)*speed;
        float travel_y=(target_y*dt+dy*carry)*speed;
        player.velocity_x-=dx*fraction;
        player.velocity_y-=dy*fraction;
        walk_body(&player.x,&player.y,travel_x,travel_y,
                  feet_z,stance+PLAYER_ABOVE_EYE,grounded||climbing?1.5f:0.f);
        if (was_crouching!=bool(in->crouch)) {
            was_crouching=in->crouch;
            stance_sound=was_crouching?CROUCH_IN:CROUCH_OUT;
        }
        if ((grounded || air_ms<=50) && !swimming && (in->move || in->strafe)) {
            bool wading=feet_z+.25f<WATER_LEVEL;
            if (!wading || WATER_LEVEL-map_cube((int)player.x,(int)player.y)->floor<=stance)
                step_sound=in->crouch?(wading?WATER_CROUCH_STEPS:CROUCH_STEPS):(wading?WATER_STEPS:FOOTSTEPS);
        }
        body_space(player.x,player.y,feet_z,&floor,&ceiling);
        if (feet_z>floor+.01f) grounded=false;
        if (climbing) {
            feet_z=fminf(fminf(ladder_top,ceiling-stance-PLAYER_ABOVE_EYE),feet_z+(swimming?8:16)*in->move*dt);
            air_ms=jump_start_ticks=0; jump_speed=0;
            grounded=feet_z<=floor;
        } else if (swimming) {
            float desired=8*in->move*sinf(player.pitch);
            jump_speed+=(desired-jump_speed)*fraction;
            feet_z=fminf(ceiling-stance-PLAYER_ABOVE_EYE,feet_z+(jump_speed-2.5f)*dt);
            grounded=feet_z<=floor;
            air_ms=jump_start_ticks=0;
        } else if (!grounded) {
            // PC movement computes displacement before assigning the jump impulse.
            // The first moving tick still uses floor friction, then air friction.
            for (unsigned i=0;i<steps && i<20;++i) {
                if (jump_start_ticks==2) { --jump_start_ticks; continue; }
                if (jump_start_ticks) { jump_start_ticks=0; jump_speed*=23.f/24; }
                else { air_ms+=5; jump_speed*=119.f/120; }
                feet_z+=(jump_speed-9.5f-air_ms/30.f)*.005f;
                if (feet_z+stance+PLAYER_ABOVE_EYE>=ceiling) { feet_z=ceiling-stance-PLAYER_ABOVE_EYE; jump_speed=0; }
                if (feet_z<=floor+.01f) { feet_z=floor; break; }
            }
        }
        if (feet_z<=floor && !jump_start_ticks) {
            if (air_ms>200) movement_sound=air_ms>800?PLAYER_LAND:SOFT_LAND;
            feet_z=floor; grounded=true; jump_speed=0; air_ms=0;
        }
        float delta=feet_z+stance-player.eye_height;
        player.eye_height+=grounded?fmaxf(-12*dt,fminf(12*dt,delta)):delta;
        player.eye_height=fmaxf(floor+.1f,fminf(ceiling-.1f,player.eye_height));
        if (swimming!=in_water) {
            if (!swimming) movement_sound=WATER_EXIT;
            else if (!last_splash || clock_ms-last_splash>500) {
                movement_sound=WATER_ENTER;
                last_splash=clock_ms;
            }
            in_water=swimming;
        }
        if (grounded || climbing || swimming) sway_ms+=elapsed;
        // hudgun.h follows the physics velocity before crouch and water scale travel.
        float gain=1-powf(.7f,100*dt);
        float vertical=climbing?in->move:jump_speed/(swimming?8.f:16.f);
        player.sway_x+=gain*(player.velocity_x-player.sway_x);
        player.sway_y+=gain*(player.velocity_y-player.sway_y);
        sway_vertical+=gain*(vertical-sway_vertical);
        if (switch_end) {
            if (clock_ms+switch_ms/2>switch_end) player.state.gunselect=next_gun;
            if (clock_ms>switch_end) switch_end=0;
        }
        if (in->swap && !switch_end && player.reload_at<=clock_ms) {
            next_gun=player.state.gunselect==GUN_PISTOL ? GUN_ASSAULT : GUN_PISTOL;
            switch_end=clock_ms+switch_ms;
            player.shots=player.shot_until=0;
        }
    }
    collect(player); update_bot(dt); collect(bot);
    // During fine two-stick tracking, share a fraction of the target's angular
    // motion. An existing aiming error never pulls the crosshair to center.
    if (!player.respawn_at && !bot.respawn_at && fabsf(in->strafe)>.05f &&
        fabsf(in->turn)>.001f && fabsf(in->turn)<.45f && fabsf(in->look)<.45f) {
        float dx=bot.x-player.x, dy=bot.y-player.y;
        float forward=dx*cosf(player.yaw)+dy*sinf(player.yaw);
        if (forward<=1 || forward>55) return;
        float side=dy*cosf(player.yaw)-dx*sinf(player.yaw);
        float chest=eye(bot)-1;
        float offset=hypotf(side,chest-eye(player)-forward*tanf(player.pitch))/forward;
        // Keep the gain steady across the torso. Fade smoothly through the
        // surrounding cone so crossing its inner edge doesn't change turn speed.
        float edge=fmaxf(0,fminf(1,(.14f-offset)/.10f));
        float proximity=classic_assist ? fmaxf(0,1-offset/.14f) : edge*edge*(3-2*edge);
        if (proximity>0 && visible(player.x,player.y,eye(player),bot.x,bot.y,chest)) {
            float bearing_change=atan2f(old_dx*dy-old_dy*dx,old_dx*dx+old_dy*dy);
            float cap=32*radians*dt;
            float strength=(classic_assist ? .65f : .86f)*proximity*fminf(1,(.45f-fabsf(in->turn))/.20f)
                           *fminf(1,fabsf(in->strafe)/.25f);
            player.yaw+=strength*fmaxf(-cap,fminf(cap,bearing_change-manual_yaw));
        }
    }
}
MatchPoint bot_model_point(const MatchView *v,MatchPoint p) {
    float st=sinf(v->bot_tilt),ct=cosf(v->bot_tilt);
    float forward=ct*p.x-st*p.z,c=cosf(v->bot_yaw),s=sinf(v->bot_yaw);
    return {v->tx+c*forward-s*p.y,v->ty+s*forward+c*p.y,
            v->bot_floor+st*p.x+ct*p.z};
}
static void bot_view(MatchView *v) {
    v->bot_yaw = bot.yaw; v->tx = bot.x; v->ty = bot.y; v->bot_floor=eye(bot)-PLAYER_EYE_HEIGHT; v->bot_alive = !bot.respawn_at;
    // The world rifle's md2emit lasts 50 ms of the 90 ms attack cue.
    v->bot_flash_end_ms = bot.flash_until>=40 ? bot.flash_until-40 : 0;
    // MD2 sequences and frame periods used by the PC renderclient.
    int first=0,count=1;
    float pose=0;
    if (bot.respawn_at) {
        first=178; count=6; pose=(clock_ms+2000-bot.respawn_at+intermission_age)/100.f;
    } else if (bot.pain_until>clock_ms) {
        first=54; count=4; pose=(clock_ms+300-bot.pain_until)/75.f;
    } else if (bot.flash_until && clock_ms<bot.flash_until+210) {
        first=46; count=8; pose=(clock_ms+90-bot.flash_until)/37.5f;
    } else if (bot_pose) {
        first=40; count=6; pose=stride_phase;
    } else {
        count=40; pose=idle_ms/100.f;
    }
    int frame=(int)fminf(pose,count-1);
    v->bot_pose=first+frame;
    v->next_bot_pose=first+(first==0 || first==40 ? (frame+1)%count : (frame+1<count ? frame+1 : frame));
    v->bot_pose_blend=v->bot_pose==v->next_bot_pose ? 0 : pose-floorf(pose);
    v->bot_tilt=bot.pitch*.25f;
    const MatchPoint &a=head_poses[v->bot_pose],&b=head_poses[v->next_bot_pose];
    float t=v->bot_pose_blend;
    v->bot_head=bot_model_point(v,{a.x+t*(b.x-a.x),a.y+t*(b.y-a.y),a.z+t*(b.z-a.z)});
}
static int target_contact(const MatchView *v,float range,bool check_geometry);
MatchSound match_attack(const MatchInput *in) {
    empty_click=false;
    if (!in->fire) pistol_trigger_used=false;
    bool fire = in->fire && (guns[player.state.gunselect].isauto || !pistol_trigger_used);
    if (switch_end && !player.respawn_at && phase==PLAYING)
        return switch_end==clock_ms+switch_ms ? WEAPON_CHANGE : SILENT;
    // PC gun::attack only observes a trigger release after gunwait expires.
    if (!fire && clock_ms>=player.ready_at) player.shots=0;
    MatchSound shot=phase == PLAYING ? trigger(player,fire,in->refill) : SILENT;
    if (shot==RIFLE_SHOT || shot==PISTOL_SHOT) {
        const int gun=player.state.gunselect;
        const guninfo &g=guns[gun];
        if (!g.isauto) pistol_trigger_used=true;
        ++player.shots;
        player.shot_until=player.ready_at;
        float ramp=(float)player.shots/g.recoilincrease;
        player.pitch_velocity=fminf(ramp*ramp+g.recoilbase*.1f,g.maxrecoil*.1f);
        // Windows aims at the world under the crosshair, from 0.2 units below
        // the eye. Resolve that depth without stalling the GPU for a readback.
        float dx=cosf(player.pitch)*cosf(player.yaw),dy=cosf(player.pitch)*sinf(player.yaw);
        float dz=sinf(player.pitch),eye_z=eye(player),z=eye_z-.2f;
        SurfaceHit hit;
        trace_surface(player.x,player.y,eye_z,player.x+512*dx,player.y+512*dy,eye_z+512*dz,&hit);
        float depth=prop_depth({player.x,player.y,eye_z},{dx,dy,dz},512*hit.fraction);
        dx*=depth; dy*=depth; dz=dz*depth+.2f;
        float length=fmaxf(.00001f,sqrtf(dx*dx+dy*dy+dz*dz));
        dx/=length; dy/=length; dz/=length;
        firing_impulse(player,gun,dx,dy,dz,in->crouch);
        int spread=gun==GUN_ASSAULT ? (player.shots>2?55:g.spread+(player.shots==1?5:15)) : g.spread;
        auto jitter=[spread]() { return ((int)(random_number()%spread)-spread/2)*.001f; };
        dx+=jitter(); dy+=jitter(); dz+=jitter();
        MatchView ray={};
        ray.px=player.x; ray.py=player.y; ray.eye=z;
        ray.angle=atan2f(dy,dx); ray.pitch=atan2f(dz,hypotf(dx,dy));
        bot_view(&ray);
        float magnitude=sqrtf(dx*dx+dy*dy+dz*dz);
        if (target_contact(&ray,length*hypotf(dx,dy),false)) {
            damage(bot,player,gun,dx,dy,dz);
            // PC raydamage shortens the effects endpoint to the struck actor.
            length=hypotf(distance(player.x,player.y,bot.x,bot.y),eye(player)-eye(bot))/magnitude;
        }
        spark_splash(player.x+length*dx,player.y+length*dy,z+length*dz);
        // Only leave a decal when the struck surface is within one map unit
        // of the endpoint. This also covers an opponent backed against a wall.
        float reach=length+1/magnitude;
        if (length*length*magnitude*magnitude>.07f &&
            trace_surface(player.x,player.y,z,player.x+reach*dx,player.y+reach*dy,z+reach*dz,&hit) &&
            fabsf(reach*hit.fraction-length)*magnitude<=1.f) {
            mark_surface(hit);
        }
    }
    return shot;
}
void match_view(MatchView *v) {
    v->empty_click=empty_click;
    v->marks=marks; v->mark_count=mark_count; v->elapsed_ms=clock_ms;
    v->bot_line=bot_line;
    if (bot_line.expires<=clock_ms+intermission_age) v->bot_line.expires=0;
    v->air=air_sound; v->impact=impact_sound;
    v->sparks=sparks; v->spark_count=spark_count;
    v->blood=blood; v->blood_count=blood_count;
    v->classic_assist=classic_assist;
    v->px = player.x; v->py = player.y; v->eye = eye(player);
    v->angle = player.yaw; v->pitch = player.pitch;
    bot_view(v);
    v->player_frags = player.frags; v->bot_frags = bot.frags;
    v->health = player.state.health; v->armour = player.state.armour;
    int gun = player.state.gunselect;
    v->magazine = player.state.mag[gun]; v->reserve = player.state.ammo[gun];
    v->pistol = gun == GUN_PISTOL;
    v->respawning = !!player.respawn_at; v->reload = player.reload_at > clock_ms;
    float lower=0;
    if (v->reload) {
        lower=sinf(3.14159265f*(player.reload_at-clock_ms)/reloadtime(gun));
    } else if (switch_end && switch_end>clock_ms) {
        lower=sinf(3.14159265f*(switch_end-clock_ms)/switch_ms);
    }
    v->weapon_kick=0;
    float speed=fminf(1,hypotf(player.velocity_x,player.velocity_y));
    float step=sway_ms/105.f, bob=(v->reload || switch_end)?0:.025f*speed*(was_crouching?.75f:1);
    float cp=cosf(player.pitch),sp=sinf(player.pitch);
    float forward=(1.5f/16)*(player.sway_x*cosf(player.yaw)+player.sway_y*sinf(player.yaw));
    float up=(.4f/16)*sway_vertical-bob*fabsf(cosf(step));
    v->gun_side=(1.5f/16)*(player.sway_y*cosf(player.yaw)-player.sway_x*sinf(player.yaw))
                +bob*sinf(step)*cp;
    v->gun_up=up*cp-forward*sp;
    v->gun_forward=forward*cp+up*sp;
    v->gun_rotation=-90*lower; v->gun_back=0;
    v->gun_pose=v->next_gun_pose=0;
    v->flash=0;
    if (!v->reload && player.shot_until>clock_ms) {
        unsigned age=attackdelay(gun)-(player.shot_until-clock_ms);
        float t=age/(float)attackdelay(gun);
        float frame=8*t;
        v->gun_pose=1+(int)frame; v->next_gun_pose=(v->gun_pose+1)%9;
        v->weapon_kick=frame-floorf(frame);
        v->flash=age<(unsigned)attackdelay(gun)/4;
        float phase=1.5f*t-1.5f;
        float kick=-sinf(phase*phase*phase);
        if (stance<PLAYER_EYE_HEIGHT) kick*=.75f;
        v->gun_rotation=guns[gun].mdl_kick_rot*kick;
        v->gun_back=guns[gun].mdl_kick_back*kick*.1f;
    }
    v->wide_crosshair = !v->pistol && player.shots>3;
    v->damage_alpha=hurt_until>clock_ms?.45f*fminf(1,(hurt_until-clock_ms)/125.f):0;
    v->bot_shot_pending=phase==PLAYING && bot_saw_player && !bot.respawn_at &&
                        bot_attack_at<=clock_ms+40 && bot.ready_at<=clock_ms+40;
    v->damage_directions=damage_directions;
    v->phase = phase; v->bot_sound = bot_sound;
    v->movement_sound=movement_sound; v->pickup_sound=pickup_sound; v->pain_sound=pain_sound;
    v->stance_sound=stance_sound;
    v->footsteps=phase==PLAYING && !player.respawn_at && clock_ms>=player.pain_until ? step_sound:SILENT;
    v->bot_footsteps=phase==PLAYING && !bot.respawn_at && bot_pose && clock_ms>=bot.pain_until &&
        hypotf(distance(bot.x,bot.y,player.x,player.y),eye(bot)-eye(player))<20 ? FOOTSTEPS:SILENT;
    v->bot_voice=bot_voice;
    v->seconds = clock_ms >= duration ? 0 : (duration-clock_ms+999)/1000;
    v->items = item_views; v->item_count = 0;
    for (int i = 0; i < item_count; ++i) if (clock_ms >= unavailable_until[i]) {
        const ItemLocation &p = locations[i];
        int icon = p.kind >= I_HEALTH ? 2+p.kind-I_HEALTH : p.kind-I_CLIPS;
        item_views[v->item_count++] = {p.x,p.y,icon,ground(p.x,p.y)+p.height_tenths*.1f};
    }
}

// Both volumes use horizontal ray distance as their parameter. Rotating into
// bot-local space makes the pitched torso an upright finite cylinder.
static float torso_contact(const MatchView *v,float c,float s,float slope,float range) {
    float cy=cosf(v->bot_yaw),sy=sinf(v->bot_yaw),ct=cosf(v->bot_tilt),st=sinf(v->bot_tilt);
    float x=v->px-v->tx,y=v->py-v->ty,z=v->eye-v->bot_floor;
    float forward=x*cy+y*sy,side=y*cy-x*sy,df=c*cy+s*sy,ds=s*cy-c*sy;
    float ox=forward*ct+z*st,oz=z*ct-forward*st;
    float dx=df*ct+slope*st,dz=slope*ct-df*st;
    float speed2=dx*dx+ds*ds,enter=0,exit=range;
    if (speed2>.000001f) {
        float closest=-(ox*dx+side*ds)/speed2;
        float rx=ox+closest*dx,ry=side+closest*ds;
        float radius2=1.1f*1.1f-rx*rx-ry*ry;
        if (radius2<0) return -1;
        float half=sqrtf(radius2/speed2);
        enter=fmaxf(enter,closest-half); exit=fminf(exit,closest+half);
    } else if (ox*ox+side*side>1.1f*1.1f) return -1;
    if (fabsf(dz)>.000001f) {
        float bottom=-oz/dz,top=(PLAYER_EYE_HEIGHT-oz)/dz;
        enter=fmaxf(enter,fminf(bottom,top)); exit=fminf(exit,fmaxf(bottom,top));
    } else if (oz<0 || oz>PLAYER_EYE_HEIGHT) return -1;
    return enter<=exit?enter:-1;
}
static int target_contact(const MatchView *v,float range,bool check_geometry) {
    if (!v->bot_alive || v->respawning) return 0;
    float c=cosf(v->angle),s=sinf(v->angle),slope=tanf(v->pitch);
    if (fabsf((v->ty-v->py)*c-(v->tx-v->px)*s)>4) return 0;
    float x=v->bot_head.x-v->px,y=v->bot_head.y-v->py,z=v->bot_head.z-v->eye;
    float norm=1+slope*slope,closest=(x*c+y*s+z*slope)/norm;
    float rx=x-closest*c,ry=y-closest*s,rz=z-closest*slope;
    float radius2=.4f*.4f-rx*rx-ry*ry-rz*rz;
    float enter=radius2>=0?closest-sqrtf(radius2/norm):-1;
    if (enter<0 || enter>range) enter=torso_contact(v,c,s,slope,range);
    else if (check_geometry) {
        // A waist-high prop can separate intersections with the two volumes.
        float torso=torso_contact(v,c,s,slope,enter);
        if (torso>=0) enter=torso;
    }
    if (enter<0) return 0;
    if (!check_geometry) return 1;
    return visible(v->px,v->py,v->eye,v->px+c*enter,v->py+s*enter,v->eye+slope*enter) &&
           prop_depth({v->px,v->py,v->eye},{c,s,slope},enter)>=enter;
}
int match_targeted(const MatchView *v) {
    return target_contact(v,512,true);
}
