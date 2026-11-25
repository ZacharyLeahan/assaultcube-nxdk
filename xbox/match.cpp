#include "match.h"
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
    unsigned respawn_at, ready_at, reload_at, flash_until;
};
Fighter player, bot;
float feet_z, stance, jump_speed, air_seconds;
bool grounded, held_jump;
unsigned clock_ms, next_route, bot_attack_at, hurt_until;
unsigned damage_directions[8];
unsigned unavailable_until[item_count], random_state;
int burst, bot_pose;
float stride_phase;
bool was_firing, bot_saw_player;
unsigned switch_end;
int next_gun;
MatchPhase phase;
bool classic_assist;
MatchSound bot_sound;
MatchSound movement_sound, pickup_sound, pain_sound;
float step_distance;
ItemView item_views[item_count];
BulletMark marks[64];
unsigned next_mark, mark_count;
ImpactSpark sparks[64];
unsigned next_spark,spark_count,effect_seed;
unsigned effect_random(unsigned limit) {
    effect_seed=1664525u*effect_seed+1013904223u;
    return (effect_seed>>8)%limit;
}
void spark_splash(const SurfaceHit &hit) {
    for (int i=0;i<5;++i) {
        short x,y,z;
        do {
            x=(int)effect_random(300)-150;
            y=(int)effect_random(300)-150;
            z=(int)effect_random(300)-150;
        } while (x*x+y*y+z*z>150*150);
        sparks[next_spark]={hit.x,hit.y,hit.z,x,y,z,clock_ms,effect_random(750)};
        next_spark=(next_spark+1)%64;
        if (spark_count<64) ++spark_count;
    }
}
unsigned random_number() {
    random_state = random_state*1664525u + 1013904223u;
    return random_state >> 8;
}
float ground(float x, float y) { return map_floor(map_cube((int)x,(int)y)); }
float distance(float x,float y,float tx,float ty) { return hypotf(tx-x,ty-y); }
float eye(const Fighter &f) { return &f==&player ? f.eye_height : ground(f.x,f.y)+3.6f; }
void spawn(Fighter &f, int index) {
    f.x = starts[index][0]+.5f; f.y = starts[index][1]+.5f;
    f.yaw = (starts[index][2]*.1f-90)*radians; f.pitch = 0;
    f.eye_height=ground(f.x,f.y)+3.6f;
    if (&f==&player) {
        switch_end=0;
        memset(damage_directions,0,sizeof(damage_directions));
        hurt_until=0;
        feet_z=ground(f.x,f.y); stance=3.6f; jump_speed=air_seconds=0;
        grounded=true; held_jump=false;
        step_distance=0;
    }
    f.velocity_x=f.velocity_y=f.turn_boost=0;
    f.sway_x=f.sway_y=0;
    f.pitch_velocity=0; f.shots=f.shot_until=0;
    f.turn_direction=0;
    f.state.respawn();
    f.state.spawnstate(GMODE_BOTDEATHMATCH);
    f.respawn_at = f.ready_at = f.reload_at = f.flash_until = 0;
}
void aim_at_opponent(Fighter &f, const Fighter &opponent) {
    f.yaw = atan2f(opponent.y-f.y,opponent.x-f.x);
    f.pitch = atan2f(eye(opponent)-1-eye(f),fmaxf(1,distance(f.x,f.y,opponent.x,opponent.y)));
}
void begin(MatchPhase initial) {
    next_mark=mark_count=0;
    next_spark=spark_count=0; effect_seed=1;
    clock_ms = 0; random_state = 7331; phase = initial;
    player.frags = bot.frags = burst = bot_pose = 0;
    stride_phase=0;
    next_route = hurt_until = 0;
    bot_attack_at = 1500; bot_saw_player = was_firing = false;
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
        if (!visible(x,y,ground(x,y)+3.6f,opponent.x,opponent.y,eye(opponent))) score += 8;
        if (score > best_score) { best_score = score; best = i; }
    }
    spawn(f,best >= 0 ? best : fallback);
    aim_at_opponent(f,opponent);
    next_route = 0; bot_attack_at = clock_ms+1200; burst = 0;
    bot_saw_player = false;
}
void damage(Fighter &victim, Fighter &attacker, int gun) {
    int dealt=victim.state.dodamage(guns[gun].damage,gun);
    if (&victim == &player) {
        unsigned remaining=hurt_until>clock_ms?hurt_until-clock_ms:0;
        hurt_until=clock_ms+fminf(3999,remaining+7*dealt);
        float relative=atan2f(attacker.y-player.y,attacker.x-player.x)-player.yaw;
        int sector=(int)floorf(relative/(45*radians)+.5f);
        damage_directions[(unsigned)sector%8]=clock_ms+1000;
        pain_sound=victim.state.health>0 ? PLAYER_PAIN : PLAYER_DEATH;
    }
    if (victim.state.health > 0) return;
    victim.state.health = 0;
    victim.respawn_at = clock_ms+2000;
    victim.flash_until = 0;
    ++attacker.frags;
    if (attacker.frags >= FRAG_LIMIT) phase = INTERMISSION;
}
MatchSound trigger(Fighter &f, bool fire, bool refill, bool on_target, Fighter &opponent) {
    int gun = f.state.gunselect;
    int &mag = f.state.mag[gun], &reserve = f.state.ammo[gun];
    if (f.ready_at > clock_ms || f.respawn_at) return SILENT;
    if ((refill || (fire && mag == 0)) && reserve > 0 && mag < magsize(gun)) {
        int count = magsize(gun)-mag;
        if (count > reserve) count = reserve;
        mag += count; reserve -= count;
        f.ready_at = f.reload_at = clock_ms+reloadtime(gun);
        return gun == GUN_PISTOL ? PISTOL_RELOAD : RIFLE_RELOAD;
    }
    if (!fire || !mag) return SILENT;
    --mag;
    f.ready_at = clock_ms+attackdelay(gun);
    f.flash_until = clock_ms+90;
    if (on_target && !opponent.respawn_at) damage(opponent,f,gun);
    return gun == GUN_PISTOL ? PISTOL_SHOT : RIFLE_SHOT;
}
void collect(Fighter &f) {
    if (f.respawn_at) return;
    for (int i = 0; i < item_count; ++i) {
        const ItemLocation &p = locations[i];
        float dz=(&f==&player?feet_z:ground(f.x,f.y))-ground(p.x,p.y)-p.height_tenths*.1f;
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
void update_bot(float dt) {
    bot_pose = 0;
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
    MatchSound shot = trigger(bot,true,false,on_target,player);
    if ((shot == RIFLE_SHOT || shot == PISTOL_SHOT) && --burst == 0)
        bot_attack_at = clock_ms+1500+random_number()%501;
    bot_sound = shot;
}
}

void match_prepare(void) { navigation_prepare(); begin(READY); }
void match_update(const MatchInput *in, unsigned elapsed, float motion_seconds) {
    bot_sound=movement_sound=pickup_sound=pain_sound=SILENT;
    if (phase != PLAYING) {
        if (in->assist_choice) classic_assist=in->assist_choice<0;
        if (in->start) begin(PLAYING);
        return;
    }
    clock_ms += elapsed;
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
        player.pitch=fmaxf(-75*radians,fminf(75*radians,player.pitch+(look*105+player.pitch_velocity*16)*radians*dt));
        player.pitch_velocity*=expf(-25*dt);
        if (player.pitch_velocity>.001f && player.pitch_velocity<.05f)
            player.pitch_velocity-=guns[player.state.gunselect].recoilbackfade*.01f;

        float floor,ceiling;
        body_space(player.x,player.y,&floor,&ceiling);
        float target=fminf(in->crouch?2.7f:3.6f,ceiling-feet_z-.9f);
        stance+=fmaxf(-8*dt,fminf(8*dt,target-stance));
        if (grounded && !held_jump && in->jump && !in->crouch && stance>=3.59f) {
            grounded=false; jump_speed=32; air_seconds=0;
            movement_sound=PLAYER_JUMP;
        }
        held_jump=in->jump;
        float speed=(stance<3.59f?6.4f:16.f)/fmaxf(1,hypotf(in->move,in->strafe));
        float c=cosf(player.yaw), s=sinf(player.yaw);
        float dx=(in->move*c-in->strafe*s)*speed-player.velocity_x;
        float dy=(in->move*s+in->strafe*c)*speed-player.velocity_y;
        float change=hypotf(dx,dy);
        float acceleration=(in->move || in->strafe) ? 128.f : 240.f;
        float fraction=change>0 ? fminf(1,acceleration*dt/change) : 0;
        player.velocity_x+=dx*fraction;
        player.velocity_y+=dy*fraction;
        float old_x=player.x,old_y=player.y;
        walk_body(&player.x,&player.y,player.velocity_x*dt,player.velocity_y*dt,
                  feet_z,stance+.9f,grounded?1.5f:0.f);
        if (grounded && !in->crouch) {
            step_distance+=distance(old_x,old_y,player.x,player.y);
            if (step_distance>=5.6f) { step_distance-=5.6f; movement_sound=PLAYER_STEP; }
        }
        if (dt>0) {
            float gain=1-powf(.7f,100*dt);
            player.sway_x+=gain*((player.x-old_x)/(16*dt)-player.sway_x);
            player.sway_y+=gain*((player.y-old_y)/(16*dt)-player.sway_y);
        }
        body_space(player.x,player.y,&floor,&ceiling);
        if (feet_z>floor+.01f) grounded=false;
        if (!grounded) {
            // PC air friction and its time-in-air-dependent downward step.
            air_seconds+=dt;
            jump_speed*=expf(-dt/.60f);
            feet_z+=(jump_speed-9.5f-air_seconds*(100.f/3))*dt;
            if (feet_z+stance+.9f>=ceiling) { feet_z=ceiling-stance-.9f; jump_speed=0; }
        }
        if (feet_z<=floor) {
            if (air_seconds>.2f) movement_sound=PLAYER_LAND;
            feet_z=floor; grounded=true; jump_speed=air_seconds=0;
        }
        float delta=feet_z+stance-player.eye_height;
        player.eye_height+=grounded?fmaxf(-12*dt,fminf(12*dt,delta)):delta;
        player.eye_height=fmaxf(floor+.1f,fminf(ceiling-.1f,player.eye_height));
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
MatchSound match_attack(const MatchInput *in) {
    bool fire = in->fire && (guns[player.state.gunselect].isauto || !was_firing);
    was_firing = in->fire;
    if (switch_end && !player.respawn_at && phase==PLAYING)
        return switch_end==clock_ms+switch_ms ? WEAPON_CHANGE : SILENT;
    if (!in->fire) player.shots=0;
    MatchSound shot=phase == PLAYING ? trigger(player,fire,in->refill,false,bot) : SILENT;
    if (shot==RIFLE_SHOT || shot==PISTOL_SHOT) {
        const int gun=player.state.gunselect;
        const guninfo &g=guns[gun];
        ++player.shots;
        player.shot_until=clock_ms+g.attackdelay;
        float ramp=(float)player.shots/g.recoilincrease;
        player.pitch_velocity=fminf(ramp*ramp+g.recoilbase*.1f,g.maxrecoil*.1f);
        int spread=gun==GUN_ASSAULT ? (player.shots>2?55:g.spread+(player.shots==1?5:15)) : g.spread;
        auto jitter=[spread]() { return ((int)(random_number()%spread)-spread/2)*.001f; };
        float dx=cosf(player.pitch)*cosf(player.yaw)+jitter();
        float dy=cosf(player.pitch)*sinf(player.yaw)+jitter();
        float dz=sinf(player.pitch)+jitter(), z=eye(player);
        MatchView ray={};
        ray.px=player.x; ray.py=player.y; ray.eye=z;
        ray.angle=atan2f(dy,dx); ray.pitch=atan2f(dz,hypotf(dx,dy));
        ray.tx=bot.x; ray.ty=bot.y; ray.bot_alive=!bot.respawn_at;
        if (match_targeted(&ray)) { damage(bot,player,gun); return shot; }
        SurfaceHit hit;
        if (trace_surface(player.x,player.y,z,player.x+512*dx,player.y+512*dy,z+512*dz,&hit)) {
            spark_splash(hit);
            marks[next_mark]={hit.x,hit.y,hit.z,hit.axis,clock_ms+10000};
            next_mark=(next_mark+1)%64;
            if (mark_count<64) ++mark_count;
        }
    }
    return shot;
}
void match_view(MatchView *v) {
    v->marks=marks; v->mark_count=mark_count; v->elapsed_ms=clock_ms;
    v->sparks=sparks; v->spark_count=spark_count;
    v->classic_assist=classic_assist;
    v->px = player.x; v->py = player.y; v->eye = eye(player);
    v->angle = player.yaw; v->pitch = player.pitch;
    v->bot_yaw = bot.yaw; v->tx = bot.x; v->ty = bot.y; v->bot_alive = !bot.respawn_at;
    v->bot_flash = bot.flash_until > clock_ms;
    v->bot_pose = bot_pose ? bot_pose : (v->bot_flash ? 7 : 0);
    v->stride_blend = bot_pose ? stride_phase-floorf(stride_phase) : 0;
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
    float speed=fminf(1,hypotf(player.sway_x,player.sway_y));
    float step=clock_ms/105.f;
    v->gun_side=.55f*(player.sway_y*cosf(player.yaw)-player.sway_x*sinf(player.yaw))
                -.26f*speed*sinf(step);
    v->gun_up=-.28f*speed*fabsf(cosf(step));
    v->gun_forward=.45f*(player.sway_x*cosf(player.yaw)+player.sway_y*sinf(player.yaw));
    v->gun_rotation=-90*lower; v->gun_back=0;
    v->gun_pose=v->next_gun_pose=0;
    v->flash=0;
    if (player.shot_until>clock_ms) {
        unsigned age=attackdelay(gun)-(player.shot_until-clock_ms);
        float t=age/(float)attackdelay(gun);
        float frame=8*t;
        v->gun_pose=1+(int)frame; v->next_gun_pose=(v->gun_pose+1)%9;
        v->weapon_kick=frame-floorf(frame);
        v->flash=age<(unsigned)attackdelay(gun)/4;
        float phase=1.5f*t-1.5f;
        float kick=-sinf(phase*phase*phase);
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
    v->seconds = clock_ms >= duration ? 0 : (duration-clock_ms+999)/1000;
    v->items = item_views; v->item_count = 0;
    for (int i = 0; i < item_count; ++i) if (clock_ms >= unavailable_until[i]) {
        const ItemLocation &p = locations[i];
        int icon = p.kind >= I_HEALTH ? 2+p.kind-I_HEALTH : p.kind-I_CLIPS;
        item_views[v->item_count++] = {p.x,p.y,icon,ground(p.x,p.y)+p.height_tenths*.1f};
    }
}

// Intersect the crosshair ray with the upright player cylinder, independently of output resolution.
int match_targeted(const MatchView *v) {
    if (!v->bot_alive || v->respawning) return 0;
    float c=cosf(v->angle), s=sinf(v->angle), dx=v->tx-v->px, dy=v->ty-v->py;
    float along=dx*c+dy*s, across=dy*c-dx*s;
    const float radius=1.0f;
    if (fabsf(across)>radius) return 0;
    float half=sqrtf(fmaxf(0,radius*radius-across*across));
    float enter=fmaxf(.01f,along-half), exit=along+half;
    float base=ground(v->tx,v->ty), slope=tanf(v->pitch);
    if (fabsf(slope)>.00001f) {
        float a=(base-v->eye)/slope,b=(base+4.6f-v->eye)/slope;
        enter=fmaxf(enter,fminf(a,b)); exit=fminf(exit,fmaxf(a,b));
    } else if (v->eye<base || v->eye>base+4.6f) return 0;
    return enter<=exit && visible(v->px,v->py,v->eye,v->px+c*enter,v->py+s*enter,v->eye+slope*enter);
}
